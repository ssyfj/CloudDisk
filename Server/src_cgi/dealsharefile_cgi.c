/*
*用于处理共享文件的大多数操作：取消分享、文件转存、文件pv字段的处理；注意这里没有实现对文件的下载操作，后面单独实现
*注意：对于Web，我们修改mysql足够，对于Client我们需要去写入redis
*/
//标准头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

//fastcgi头文件
#include "fcgi_config.h"
#include "fcgi_stdio.h"

//fastdfs相关头文件
#include "fastdfs/tracker_types.h"				//一些定义存放在这里
#include "fastdfs/fdfs_client.h"
#include "fastcommon/logger.h"

//自定义头文件
#include "deal_mysql.h"							//处理数据库模块
#include "cfg.h"								//配置信息
#include "make_log.h"							//日志处理模块
#include "redis_keys.h"							//存放我们在redis中专门使用的key	
#include "redis_op.h"							//处理redis模块
#include "util_cgi.h" 							//cgi后台通用接口
#include "cJSON.h"								//JSON数据处理

#define 	DEALSHAREFILE_LOG_MODULE			"cgi"
#define 	DEALSHAREFILE_LOG_PROC			    "dealsharefile"

//mysql 数据库配置信息 用户名， 密码， 数据库名称
static char mysql_user[128] = {0};
static char mysql_pwd[128] = {0};
static char mysql_db[128] = {0};

//redis 服务器ip、端口
static char redis_ip[30] = {0};
static char redis_port[10] = {0};

//======================读取配置信息======================
void read_cfg()
{
    //读取mysql数据库配置信息
	get_mysql_info(mysql_user,mysql_pwd,mysql_db);
    LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]", mysql_user, mysql_pwd, mysql_db);

    //读取redis配置信息
    get_redis_info(redis_ip,redis_port);
    LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "redis:[ip=%s,port=%s]\n", redis_ip, redis_port);
}

//======================解析的json包======================
int get_json_info(char *buf, char *user, char *token, char *md5, char *filename)
{
    int ret = 0;

    /*json数据如下
    {
    "user": "xxxx",
    "token": "xxxx",
    "md5": "xxx",
    "filename": "xxx"
    }
    */

    //解析json包

    //解析一个json字符串为cJSON对象
    cJSON * root = cJSON_Parse(buf);
    if(NULL == root)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //用户
    cJSON *child1 = cJSON_GetObjectItem(root, "user");
    if(NULL == child1)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    //LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "child1->valuestring = %s\n", child1->valuestring);
    strcpy(user, child1->valuestring); //拷贝内容

    //token
    cJSON *child4 = cJSON_GetObjectItem(root, "token");
    if(NULL == child4)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(token, child4->valuestring); //拷贝内容

    //文件md5码
    cJSON *child2 = cJSON_GetObjectItem(root, "md5");
    if(NULL == child2)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(md5, child2->valuestring); //拷贝内容

    //文件名字
    cJSON *child3 = cJSON_GetObjectItem(root, "filename");
    if(NULL == child3)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(filename, child3->valuestring); //拷贝内容

END:
    if(root != NULL)
    {
        cJSON_Delete(root);//删除json对象
        root = NULL;
    }

    return ret;
}

/*
======================返回后台状态，json数据返回给前端======================
*/
void return_sharefile_status(char *status_num,char *message)
{
    char *out = NULL;
    cJSON *root = cJSON_CreateObject();     //创建json对象
    cJSON_AddStringToObject(root,"code",status_num);
    cJSON_AddStringToObject(root,"token",message);
    out = cJSON_Print(root);                //json转字符串

    cJSON_Delete(root);
    
    if(out != NULL)
    {
        printf("%s",out);                        //反馈数据给前端
        free(out);
    }
}


/*
======================取消分享======================
*/
int cancel_share_file(char *user, char *md5, char *filename)
{
    int ret = 0,ret2 = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    redisContext * redis_conn = NULL;
    char fileid[1024] = {0};

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "redis connected error");
        ret = -1;
        goto END;
    }

    //连接mysql
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }

    //----------------1.操作数据库-------------------
    mysql_query(conn, "set names utf8");            //设置数据库编码，主要处理中文编码问题

    //1.首先去查看该文件是不是属于本用户的共享文件
    sprintf(sql_cmd,"select id from share_file_list where user = '%s' and md5 = '%s' and file_name = '%s'",user,md5,filename);
    ret2 = process_result_one(conn, sql_cmd, NULL); //执行sql语句
    if(ret2 == 1 || ret2 == -1)                     //没有查询到数据，表示这个共享文件不存在或者不是本人分享的！！
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s] or the file[%s] is not shared by user[%s]\n", sql_cmd,filename,user);
        ret = -1;
        goto END;
    }

    //2.设置mysql语句，更新文件的共享状态，shared_status = 0为不共享
    sprintf(sql_cmd, "update user_file_list set shared_status = 0 where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);

    if (mysql_query(conn, sql_cmd) != 0)            //执行mysql语句
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //3.查询共享文件数量，进行更新
    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", "share_user");
    int count = 0;
    char tmp[512] = {0};
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if(ret2 != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //4.更新用户文件数量count字段
    count = atoi(tmp);
    if(count == 1)
    {
        sprintf(sql_cmd, "delete from user_file_count where user = '%s'", "share_user"); 
    }
    else
    {
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count-1, "share_user");
    }

    if(mysql_query(conn, sql_cmd) != 0)             //执行sql语句
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //5.删除在共享列表的数据
    sprintf(sql_cmd, "delete from share_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //----------------2.操作redis缓存-------------------
    sprintf(fileid, "%s%s", md5, filename);         //文件标识，md5+文件名

    //1.有序集合删除指定成员
    ret = rop_zset_zrem(redis_conn, FILE_PUBLIC_ZSET, fileid);
    if(ret != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to operate rop_zset_zrem\n");
        goto END;
    }

    //2.从hash移除相应记录
    ret = rop_hash_del(redis_conn, FILE_NAME_HASH, fileid);
    if(ret != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to operate rop_hash_del\n");
        goto END;
    }

END:
    if(ret == 0)
    {
        return_sharefile_status("018","success");
    }
    else
    {
        return_sharefile_status("019","failed");
    }

    if(redis_conn != NULL)
    {
        rop_disconnect(redis_conn);
    }


    if(conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}


/*
======================转存分享的文件======================
* 返回值：0成功，-1转存失败，-2文件已存在
*/
int save_file(char *user, char *md5, char *filename)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    int ret2 = 0;
    char tmp[512] = {0};

    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }

    mysql_query(conn, "set names utf8");

    //1.检查该文件是不是共享的,查询共享文件是否存在，返回用户名
    sprintf(sql_cmd,"select user from share_file_list where md5 = '%s' and file_name = '%s'",md5,filename); 
    ret2 = process_result_one(conn,sql_cmd,tmp);
    if(ret2 == 0)                                   //如果查询到数据，保存在tmp中
    {
        if(strcmp(tmp,user) == 0)                   //查询到数据，并且该用户就是自己，则返回-2
        {
            ret = -2;
            goto END;
        }
        //否则，正常，这个文件是共享文件，我们可以进行下面的操作
    }

    //2.查看此用户，文件名和md5是否存在，如果存在说明此文件存在
    sprintf(sql_cmd, "select * from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);
    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    ret2 = process_result_one(conn, sql_cmd, NULL); //执行sql语句, 最后一个参数为NULL
    if(ret2 == 2)                                   //如果有结果，说明此用户已有此文件
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "%s[filename:%s, md5:%s] exists\n", user, filename, md5);
        ret = -2;                                   //返回-2错误码
        goto END;
    }

    //3.文件信息表，查找该文件的引用计数器，用于更新
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if(ret2 != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    int count = atoi(tmp); 

    //4.修改file_info中的count字段，+1 （count 文件引用计数）
    sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", count+1, md5);
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //5.user_file_list插入一条文件数据
    sprintf(sql_cmd, "insert into user_file_list(user, md5, file_name, shared_status, pv) values ('%s', '%s', '%s', %d, %d)", user, md5, filename, 0, 0);
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //6.查询用户文件数量，更新该count字段
    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
    count = 0;

    ret2 = process_result_one(conn, sql_cmd, tmp);  //指向sql语句
    if(ret2 == 1)                                   //没有记录，插入记录
    {
        sprintf(sql_cmd, " insert into user_file_count (user, count) values('%s', %d)", user, 1);
    }
    else if(ret2 == 0)                              //有记录，更新用户文件数量count字段
    {
        count = atoi(tmp);
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count+1, user);
    }

    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

END:
    /*
    返回值：0成功，-1转存失败，-2文件已存在
    转存文件：
        成功：{"code":"020"}
        文件已存在：{"code":"021"}
        失败：{"code":"022"}
    */
    if(ret == 0)
    {
        return_sharefile_status("020","success");
    }
    else if(ret == -1)
    {
        return_sharefile_status("022","failed");
    }
    else if(ret == -2)
    {
        return_sharefile_status("021","exists");
    }

    if(conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}


/*
======================文件pv字段处理======================
*/
int pv_file(char *md5, char *filename)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    char tmp[512] = {0};
    int ret2 = 0;
    redisContext * redis_conn = NULL;
    char fileid[1024] = {0};
    int pv = 0;

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "redis connected error");
        ret = -1;
        goto END;
    }

    //连接mysql数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }
    //----------------1.操作数据库-------------------

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //1.查看该共享文件的pv字段，后面进行+1更新
    sprintf(sql_cmd, "select pv from share_file_list where md5 = '%s' and file_name = '%s'", md5, filename);
    LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "sql:%s\n", sql_cmd);

    ret2 = process_result_one(conn, sql_cmd, tmp);  //执行sql语句
    if(ret2 == 0)
    {
        pv = atoi(tmp);                             //pv字段
    }
    else
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //2.更新该文件pv字段，+1
    sprintf(sql_cmd, "update share_file_list set pv = %d where md5 = '%s' and file_name = '%s'", pv+1, md5, filename);
    LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "sql:%s\n", sql_cmd);
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //----------------2.操作redis数据库-------------------
    sprintf(fileid, "%s%s", md5, filename);         //文件标示，md5+文件名

    //1.判断元素是否在集合中(redis操作)
    ret2 = rop_zset_exit(redis_conn, FILE_PUBLIC_ZSET, fileid);
    if(ret2 == 1)                                   //2.redis存在相关缓存数据，则对应的score会+1
    {                                               //2.如果存在，有序集合score+1
        ret = rop_zset_increment(redis_conn, FILE_PUBLIC_ZSET, fileid);
        if(ret != 0)
        {
            LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "Failed to rop_zset_increment\n");
        }
    }
    else if(ret2 == 0)                              //3.不存在，则新增一个缓存数据到redis中去！
    {                                               //3.如果不存在，从mysql导入数据
        //4.redis集合中增加一个元素(redis操作)
        rop_zset_add(redis_conn, FILE_PUBLIC_ZSET, pv+1, fileid);

        //5.redis对应的hash也需要变化 (redis操作)
        rop_hash_set(redis_conn, FILE_NAME_HASH, fileid, filename);

    }
    else                                            //出错
    {
        ret = -1;
        goto END;
    }


END:
    /*
    下载文件pv字段处理
        成功：{"code":"016"}
        失败：{"code":"017"}
    */
    if(ret == 0)
    {
        return_sharefile_status("016","success");
    }
    else
    {
        return_sharefile_status("017","failed");
    }

    if(redis_conn != NULL)
    {
        rop_disconnect(redis_conn);
    }

    if(conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}


int main()
{
    char cmd[20];
    int cmd_len = 0;
    char user[USER_NAME_LEN];                       //用户名
    char token[TOKEN_LEN];                          //token
    char md5[MD5_LEN];                              //文件md5码
    char filename[FILE_NAME_LEN];                   //文件名字
    int ret = 0;

    //读取数据库和redis配置信息
    read_cfg();

    //阻塞等待用户连接
    while (FCGI_Accept() >= 0)
    {
         // 获取URL地址 "?" 后面的内容
        char *query = getenv("QUERY_STRING");

        //解析命令
        query_parse_key_value(query, "cmd", cmd, &cmd_len);
        LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "cmd = %s\n", cmd);

        char *contentLength = getenv("CONTENT_LENGTH");
        int len;

        printf("Content-type: text/html\r\n\r\n");

        if( contentLength == NULL )
        {
            len = 0;
        }
        else
        {
            len = atoi(contentLength);              //字符串转整型
        }

        if (len <= 0)
        {
            printf("No data from standard input.<p>\n");
            LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "len = 0, No data from standard input\n");
        }
        else
        {
            char buf[4*1024] = {0};
            int ret = 0;
            ret = fread(buf, 1, len, stdin);        //从标准输入(web服务器)读取内容
            if(ret == 0)
            {
                LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                continue;
            }

            LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "buf = %s\n", buf);

            get_json_info(buf, user, token, md5, filename); //解析json信息
            LOG(DEALSHAREFILE_LOG_MODULE, DEALSHAREFILE_LOG_PROC, "user = %s, token = %s, md5 = %s, file_name = %s\n", user, token, md5, filename);

            ret = verify_token(user, token);        //验证登陆token，成功返回0，失败-1
            if(ret != 0)
            {
                return_sharefile_status("111","failed"); //token验证失败错误码
                continue;                           //跳过本次循环
            }

            if(strcmp(cmd, "pv") == 0) //文件下载标志处理
            {
                pv_file(md5, filename);
            }
            else if(strcmp(cmd, "cancel") == 0) //取消分享文件
            {
                cancel_share_file(user, md5, filename);
            }
            else if(strcmp(cmd, "save") == 0) //转存文件
            {
                save_file(user, md5, filename);
            }
        }
    }

    return 0;
}
