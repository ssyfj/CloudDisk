/*
*主要用于显示文件列表，用在client的请求处理！
*/
//标准头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

//fastcgi头文件
#include "fcgi_config.h"
#include "fcgi_stdio.h"

//自定义头文件
#include "deal_mysql.h"                         //处理数据库模块
#include "cfg.h"                                //配置信息
#include "make_log.h"                           //日志处理模块
#include "redis_keys.h"                         //存放我们在redis中专门使用的key
#include "redis_op.h"                           //处理redis模块
#include "util_cgi.h"                           //cgi后台通用接口
#include "cJSON.h"                              //JSON数据处理

#define     SHAREFILES_LOG_MODULE       "cgi"
#define     SHAREFILES_LOG_PROC         "sharefiles"


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
    LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]", mysql_user, mysql_pwd, mysql_db);

    //读取redis配置信息
    get_redis_info(redis_ip,redis_port);
    LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "redis:[ip=%s,port=%s]\n", redis_ip, redis_port);
}


/*
======================返回后台文件数量状态，json数据返回给前端======================
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
        printf("%s",out);                   //反馈数据给前端
        free(out);
    }
}

/*
======================解析json数据包，获取一页数据的start与count======================
json数据如下
{
    "start": 0
    "count": 10
}
*/
int get_fileslist_json_info(char *buf, int *p_start, int *p_count)
{
    int ret = 0;
    
    cJSON * root = cJSON_Parse(buf);        //解析一个json字符串为cJSON对象
    if(NULL == root)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //文件起点
    cJSON *child2 = cJSON_GetObjectItem(root, "start");
    if(NULL == child2)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    *p_start = child2->valueint;

    //文件请求个数
    cJSON *child3 = cJSON_GetObjectItem(root, "count");
    if(NULL == child3)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    *p_count = child3->valueint;

END:
    if(root != NULL)
    {
        cJSON_Delete(root);             //删除json对象
        root = NULL;
    }

    return ret;
}


/*
======================获取共享文件个数======================
*/
int get_share_files_count(char* count)
{
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    long line = 0;
    int ret = 0;

    if(count == NULL)
    {
        ret = -1;
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "count is NULL\n");
        goto END;
    }

    //connect the database
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    sprintf(sql_cmd, "select count from user_file_count where user='%s'", "share_user");
    int ret2 = process_result_one(conn, sql_cmd, count); 
    if(ret2 != 0)               //1或者2，表示没有获取到数据到count中
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

END:
    if(conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}

/*
======================获取共享文件列表(详细信息）:正常按创建时间排序======================
//获取用户文件信息 127.0.0.1:80/sharefiles&cmd=normal
*/
int get_share_filelist(int start, int count)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    cJSON *root = NULL;
    cJSON *array =NULL;
    MYSQL_RES *res_set = NULL;
    char *out = NULL;

    //连接数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //sql语句，进行多表联合查询
    sprintf(sql_cmd, "select share_file_list.*, file_info.url, file_info.size, file_info.type from file_info, share_file_list where file_info.md5 = share_file_list.md5 limit %d, %d", start, count);
    LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Execute sql:[%s]....\n", sql_cmd);

    if (mysql_query(conn, sql_cmd) != 0)            //执行sql语句
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    res_set = mysql_store_result(conn);             //获取结果集，之后进行遍历，添加到返回的json中去
    if (res_set == NULL)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "smysql_store_result error!\n");
        ret = -1;
        goto END;
    }

    ulong line = mysql_num_rows(res_set);           //mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    if (line == 0)                                  //没有数据
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "mysql_num_rows(res_set) failed\n");
        ret = -1;
        goto END;
    }

    MYSQL_ROW row;                                  //用于遍历每一个数据

    root = cJSON_CreateObject();                    //创建json对象
    array = cJSON_CreateArray();

    // mysql_fetch_row从使用mysql_store_result得到的结果结构中提取一行，并把它放到一个行结构中。当数据用完或发生错误时返回NULL.
    while ((row = mysql_fetch_row(res_set)) != NULL)
    {
        cJSON* item = cJSON_CreateObject();
        /*
        数据库查询的每一行格式如下：
        {
            "user": "test",
            "md5": "e8ea6031b779ac26c319ddf949ad9d8d",
            "create_time": "2020-06-21 21:35:25",
            "file_name": "test.mp4",
            "share_status": 0,
            "pv": 0,
            "url": "http://192.168.1.128/group1/M00/00/00/wKgfbViy2Z2AJ-FTAaM3As-g3Z0782.mp4",
            "size": 27473666,
            "type": "mp4"
        }
        */

        int column_index = 1;                        //避开了id信息，不需要！！！

        //注意：下面字段需要进行对应我们数据库设计的字段顺序！！！
        if(row[column_index] != NULL)                //user   文件所属用户
        {
            cJSON_AddStringToObject(item, "user", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)               //md5 文件md5
        {
            cJSON_AddStringToObject(item, "md5", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)               //filename 文件名字
        {
            cJSON_AddStringToObject(item, "file_name", row[column_index]);
        }

        cJSON_AddNumberToObject(item, "share_status", 1);   //shared_status 共享状态, 0为没有共享， 1为共享

        column_index++;
        if(row[column_index] != NULL)               //pv 文件下载量，默认值为0，下载一次加1
        {
            cJSON_AddNumberToObject(item, "pv", atol( row[column_index] ));
        }

        column_index++;
        if(row[column_index] != NULL)               //createtime 文件创建时间
        {
            cJSON_AddStringToObject(item, "create_time", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)               //url 文件url
        {
            cJSON_AddStringToObject(item, "url", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)               //size 文件大小, 以字节为单位
        {
            cJSON_AddNumberToObject(item, "size", atol( row[column_index] ));
        }

        column_index++;
        if(row[column_index] != NULL)               //type 文件类型： png, zip, mp4……
        {
            cJSON_AddStringToObject(item, "type", row[column_index]);
        }

        cJSON_AddItemToArray(array, item);
    }

    cJSON_AddItemToObject(root, "files", array);    //json创建完毕！！！

    out = cJSON_Print(root);                        //进行格式化输出！！

    LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "%s\n", out);

END:
    if(ret == 0)
    {
        printf("%s", out);                          //给前端反馈文件列表信息
    }
    else
    {   
        return_sharefile_status("015","failed");    //给前端反馈错误码
    }

    if(res_set != NULL)
    {
        mysql_free_result(res_set);                 //完成所有对数据的操作后，调用mysql_free_result来善后处理
    }

    if(conn != NULL)
    {
        mysql_close(conn);
    }

    if(root != NULL)
    {
        cJSON_Delete(root);
    }

    return ret;
}

/*
======================获取共享文件排行版（简要信息）:按下载量降序排序======================
//按下载量降序127.0.0.1:80/sharefiles?cmd=pvdesc
注意：由于我们只需要简要信息，所以同之前方式存放的redis一致（fileid,filename,score表示下载量）！！！！
{
    "filename": "test.mp4",
    "pv": 0
}

注意：需要同时操作redis和mysql
a) mysql共享文件数量和redis共享文件数量对比，判断是否相等
b) 如果不相等，清空redis数据，从mysql中导入数据到redis (mysql和redis交互)
c) 从redis读取数据，给前端反馈相应信息
*/
int get_ranking_filelist(int start, int count)
{

    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    cJSON *root = NULL;
    RVALUES value = NULL;
    cJSON *array =NULL;
    char tmp[512] = {0};
    int ret2 = 0;
    MYSQL_RES *res_set = NULL;
    redisContext * redis_conn = NULL;
    char *out = NULL;

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "redis connected error");
        ret = -1;
        goto END;
    }

    //-------------------------1.操作mysql和redis检查数量是否一致-------------------------
    //连接数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //1.查询mysql共享文件数量
    sprintf(sql_cmd, "select count from user_file_count where user='%s'", "share_user");
    ret2 = process_result_one(conn, sql_cmd, tmp);  //指向sql语句
    if(ret2 != 0)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    int sql_num = atoi(tmp);                        //字符串转长整形

    //2.查看redis共享文件数量
    int redis_num = rop_zset_zcard(redis_conn, FILE_PUBLIC_ZSET);
    if(redis_num == -1)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec rop_zset_zcard\n");
        ret = -1;
        goto END;
    }

    LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "sql_num = %d, redis_num = %d\n", sql_num, redis_num);

    //3.mysql共享文件数量和redis共享文件数量对比，判断是否相等
    if(redis_num != sql_num)                        //4.如果不相等，清空redis数据，重新从mysql中导入数据到redis (mysql和redis交互)
    {
        //a) 清空redis有序数据
        rop_del_key(redis_conn, FILE_PUBLIC_ZSET);
        rop_del_key(redis_conn, FILE_NAME_HASH);

        //b) 从mysql中导入数据到redis
        strcpy(sql_cmd, "select md5, file_name, pv from share_file_list order by pv desc");
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Execute sql:[%s]....\n", sql_cmd);

        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec sql:[%s], Error: [%s]\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }

        res_set = mysql_store_result(conn);                                    //获取结果集
        if (res_set == NULL)
        {
            LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "smysql_store_result error!\n");
            ret = -1;
            goto END;
        }

        //mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
        ulong line = mysql_num_rows(res_set);
        if (line == 0)
        {
            LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "mysql_num_rows(res_set) failed\n");
            ret = -1;
            goto END;
        }

         MYSQL_ROW row;
        // mysql_fetch_row从使用mysql_store_result得到的结果结构中提取一行，并把它放到一个行结构中。当数据用完或发生错误时返回NULL.
        while ((row = mysql_fetch_row(res_set)) != NULL)
        {
            if(row[0] == NULL || row[1] == NULL || row[2] == NULL)              //md5, filename, pv
            {
                LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "mysql_fetch_row(res_set)) failed\n");
                ret = -1;
                goto END;
            }

            char fileid[1024] = {0};
            sprintf(fileid, "%s%s", row[0], row[1]);                            //文件标识，md5+文件名

            //增加有序集合成员
            rop_zset_add(redis_conn, FILE_PUBLIC_ZSET, atoi(row[2]), fileid);   //设置了共享文件fileid和pv下载量
            //增加hash记录
            rop_hash_set(redis_conn, FILE_NAME_HASH, fileid, row[1]);           //设置hash，包括fileid和filename
        }
    }

    //5.从redis读取数据，给前端反馈相应信息
    value  = (RVALUES)calloc(count, VALUES_ID_SIZE);                            //堆区请求空间
    if(value == NULL)
    {
        ret = -1;
        goto END;
    }

    int n = 0;
    int end = start + count - 1;                                                //加载资源的结束位置

    //降序获取有序集合的元素,从start到end，将结果fileid存放在value，获取value个数n
    //注意：这里是获取了全部的数据！！！！
    ret = rop_zset_zrevrange(redis_conn, FILE_PUBLIC_ZSET, start, end, value, &n);  
    if(ret != 0)
    {
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec rop_zset_zrevrange\n");
        goto END;
    }

    root = cJSON_CreateObject();
    array = cJSON_CreateArray();
    //遍历元素个数
    for(int i = 0; i < n; ++i)                                                  //遍历values中的fileid
    {
        cJSON* item = cJSON_CreateObject();

        char filename[1024] = {0};                                              //filename 文件名字
        ret = rop_hash_get(redis_conn, FILE_NAME_HASH, value[i], filename);     //获取filename
        if(ret != 0)
        {
            LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec rop_hash_get\n");
            ret = -1;
            goto END;
        }
        cJSON_AddStringToObject(item, "filename", filename);                    //添加到json


        int score = rop_zset_get_score(redis_conn, FILE_PUBLIC_ZSET, value[i]); //pv 文件下载量
        if(score == -1)
        {
            LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "Failed to exec rop_zset_get_score\n");
            ret = -1;
            goto END;
        }
        cJSON_AddNumberToObject(item, "pv", score);

        cJSON_AddItemToArray(array, item);
    }

    cJSON_AddItemToObject(root, "files", array);

    out = cJSON_Print(root);
    LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "%s\n", out);
END:
    if(ret == 0)
    {
        printf("%s", out);                                                      //给前端反馈文件列表信息
    }
    else
    {   
        return_sharefile_status("015","failed");                                     //给前端反馈错误码
    }

    if(res_set != NULL)
    {
        mysql_free_result(res_set);                                             //完成所有对数据的操作后，调用mysql_free_result来善后处理
    }

    if(redis_conn != NULL)
    {
        rop_disconnect(redis_conn);
    }

    if(conn != NULL)
    {
        mysql_close(conn);
    }

    if(value != NULL)
    {
        free(value);
    }

    if(root != NULL)
    {
        cJSON_Delete(root);
    }

    return ret;
}

int main()
{
    char cmd[20];
    char count[20];
    int ret = 0;

    //读取数据库配置信息
    read_cfg();

    //阻塞等待用户连接
    while (FCGI_Accept() >= 0)
    {

        // 获取URL地址 "?" 后面的内容
        char *query = getenv("QUERY_STRING");

        //解析命令
        query_parse_key_value(query, "cmd", cmd, NULL);
        LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "cmd = %s\n", cmd);

        printf("Content-type: text/html\r\n\r\n");

        if (strcmp(cmd, "count") == 0)                                  //count 获取用户文件个数
        {
            ret = get_share_files_count(count);
            if(ret != 0)
            {
                return_sharefile_status("111","failed");                 //文件获取失败
                continue;                                           //跳过本次循环
            }

            return_sharefile_status("110",count);                        //返回文件数量                                    //获取共享文件个数
        }
        else                                                            //获取文件列表，包括详细信息和简要降序信息
        {
            char *contentLength = getenv("CONTENT_LENGTH");
            int len;

            if( contentLength == NULL )
            {
                len = 0;
            }
            else
            {
                len = atoi(contentLength);                              //字符串转整型
            }

            if (len <= 0)
            {
                printf("No data from standard input.<p>\n");
                LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "len = 0, No data from standard input\n");
            }
            else
            {
                char buf[4*1024] = {0};
                int ret = 0;
                ret = fread(buf, 1, len, stdin);                        //从标准输入(web服务器)读取内容
                if(ret == 0)
                {
                    LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                    continue;
                }

                LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "buf = %s\n", buf);

                //获取共享文件x详细信息 127.0.0.1:80/sharefiles&cmd=normal
                //按下载量降序列表 127.0.0.1:80/sharefiles?cmd=pvdesc

                int start; //文件起点
                int count; //文件个数
                get_fileslist_json_info(buf, &start, &count);       //通过json包获取信息
                LOG(SHAREFILES_LOG_MODULE, SHAREFILES_LOG_PROC, "start = %d, count = %d\n", start, count);
                 if (strcmp(cmd, "normal") == 0)
                 {
                    get_share_filelist(start, count);               //获取共享文件列表
                 }
                 else if(strcmp(cmd, "pvdesc") == 0)
                 {
                    get_ranking_filelist(start, count);             //获取共享文件排行版，简要信息
                 }
            }
        }
    }

    return 0;
}

