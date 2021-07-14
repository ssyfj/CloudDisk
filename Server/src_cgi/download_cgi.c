/*
*主要用于文件下载，分为web端下载和client下载
*对于web端，下载小文件即可。
*对于client使用多线程/线程池，对文件进行分片下载，需要同时修改client和server端
*注意：还需要提供一个提取码下载功能
*/

#define _XOPEN_SOURCE       /* See feature_test_macros(7) */
#include <time.h>

//标准头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

//fastcgi头文件
#include "fcgi_config.h"
#include "fcgi_stdio.h"

//fastfds头文件
#include "fastdfs/tracker_types.h"              //一些定义存放在这里
#include "fastdfs/fdfs_client.h"
#include "fastcommon/logger.h"

//自定义头文件
#include "deal_mysql.h"                         //处理数据库模块
#include "cfg.h"                                //配置信息
#include "make_log.h"                           //日志处理模块
#include "redis_keys.h"                         //存放我们在redis中专门使用的key
#include "redis_op.h"                           //处理redis模块
#include "util_cgi.h"                           //cgi后台通用接口
#include "cJSON.h"                              //JSON数据处理

#define     DOWNLOAD_LOG_MODULE       "cgi"
#define     DOWNLOAD_LOG_PROC         "download"

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
    LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]", mysql_user, mysql_pwd, mysql_db);

    //读取redis配置信息
    get_redis_info(redis_ip,redis_port);
    LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "redis:[ip=%s,port=%s]\n", redis_ip, redis_port);
}


/*
======================返回后台文件数量状态，json数据返回给前端======================
*/
void return_download_status(char *status_num,char *message)
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

//======================解析的json包======================
//1.对于本人文件和共享文件使用这个方法进行解析数据
/*json数据如下
{
    "user": "xxxx",
    "token": "xxxx",
    "md5": "xxx",
    "filename": "xxx"
}
*/
int get_download_json_info(char *buf, char *user, char *token, char *md5, char *filename)
{
    int ret = 0;


    //解析json包

    //解析一个json字符串为cJSON对象
    cJSON * root = cJSON_Parse(buf);
    if(NULL == root)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //用户
    cJSON *child1 = cJSON_GetObjectItem(root, "user");
    if(NULL == child1)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    //LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "child1->valuestring = %s\n", child1->valuestring);
    strcpy(user, child1->valuestring); //拷贝内容

    //token
    cJSON *child4 = cJSON_GetObjectItem(root, "token");
    if(NULL == child4)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(token, child4->valuestring); //拷贝内容

    //文件md5码
    cJSON *child2 = cJSON_GetObjectItem(root, "md5");
    if(NULL == child2)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(md5, child2->valuestring); //拷贝内容

    //文件名字
    cJSON *child3 = cJSON_GetObjectItem(root, "filename");
    if(NULL == child3)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_GetObjectItem err\n");
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

//2.对于提取码下载，只需要按照下面方式解析即可
/*json数据如下
{
    "user":xxxx
    "token": "9e894efc0b2a898a82765d0a7f2c94cb",
    "code":"dasfg2ge"
}
*/
int get_code_json_info(char *buf, char *user, char *token,char *extracode)
{
    int ret = 0;

    //解析一个json字符串为cJSON对象
    cJSON * root = cJSON_Parse(buf);
    if(NULL == root)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //返回指定字符串对应的json对象
    cJSON *child1 = cJSON_GetObjectItem(root, "user");          //用户
    if(NULL == child1)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(user, child1->valuestring);                          //拷贝内容

    cJSON *child2 = cJSON_GetObjectItem(root, "token");         //登陆token
    if(NULL == child2)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(token, child2->valuestring);                         //拷贝内容

    cJSON *child3 = cJSON_GetObjectItem(root, "code");          //文件提取码
    if(NULL == child3)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(extracode, child3->valuestring);                         //拷贝内容
END:
    if(root != NULL)
    {
        cJSON_Delete(root);                                     //删除json对象
        root = NULL;
    }

    return ret;
}
/*
======================校验文件所属，是本人文件，还是共享文件？否则不允许下载======================
*/
int check_file_status(char *filename,char *md5,char *user)
{

    int ret = -1;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    redisContext * redis_conn = NULL;
    char tmp[512] = {0};
    char fileid[1024] = {0};
    int ret2;

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "redis connected error");
        ret = -1;
        goto END;
    }

    //===1、先判断此文件是否存在redis中，如果存在，直接返回0，允许下载
    //文件标示，md5+文件名
    sprintf(fileid, "%s%s", md5, filename);

    ret2 = rop_zset_exit(redis_conn, FILE_PUBLIC_ZSET, fileid);
    if(ret2 == 1) //存在,允许下载
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "redis exist share file[%s], allow download\n",filename);
        ret = 0;
        goto END;
    }

    //===2、如果reis没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
    //处理数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //查看此文件是否已经分享
    sprintf(sql_cmd, "select pv from share_file_list where md5 = '%s' and file_name = '%s'", md5, filename);
    //执行语句，获取一条结果
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句, 最后一个参数为NULL
    if(ret2 == 0) //说明有结果，别人已经分享此文件
    {
        //===3、如果mysql有记录，而redis没有记录，说明redis没有保存此文件，redis保存此文件信息后，再中断操作(redis操作)
        //redis保存此文件信息
        rop_zset_add(redis_conn, FILE_PUBLIC_ZSET, atoi(tmp), fileid);
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql exist share file[%s], allow download\n",filename);
        ret = 0;
        goto END;
    }

    //===4、如果此文件没有被分享，mysql查看是否是自己本人的文件
    sprintf(sql_cmd, "select id from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user,md5,filename);
    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    ret2 = process_result_one(conn, sql_cmd, NULL); //执行sql语句
    if(ret2 == 2) //有
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql exist my file[%s], allow download\n",filename);
        ret = 0;
        goto END;        
    }

END:
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
======================web文件下载方法，较为简单，提供url即可======================后面进行优化返回Blob数据给web客户端
*/
int get_web_files_url_size(char *filename,char *md5,char* url,char *size)
{
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    MYSQL_ROW row;
    MYSQL_RES *res_set = NULL;          //结果集指针
    long line = 0;
    int ret = 0;

    //连接数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    sprintf(sql_cmd, "select url,size from file_info where md5='%s'", md5);
    

    if(mysql_query(conn,sql_cmd) != 0)  //进行数据查询
    {
        print_error(conn,"mysql_query error!\n");
        ret = -1;
        goto END;
    }

    res_set = mysql_store_result(conn); //生成结果集
    if(res_set == NULL)
    {
        print_error(conn,"mysql_store_result error!\n");
        ret = -1;
        goto END;
    }

    line = mysql_num_rows(res_set);
    if(line == 0)
    {
        ret = -1;                        //没有查询到数据
        goto END;
    }

    //可能存在多个数据，我们只需要去获取一行数据
    if((row = mysql_fetch_row(res_set)) != NULL)
    {
        strcpy(url,row[0]);
        strcpy(size,row[1]);
    }

END:
    if(res_set != NULL)
    {
        mysql_free_result(res_set);                                             //完成所有对数据的操作后，调用mysql_free_result来善后处理
    }


    if(conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}


/*
======================获取提取码对应的数据======================
*/
int get_code_url(char *code,char *url,char *size,char *filename,char *md5)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    char tmp[512] = {0};
    int ret2 = 0;
    redisContext * redis_conn = NULL;
    char codeVal[1024] = {0};
    char us[2][1024] = {0};
    char createtime[128] = {0};
    char effecttime[10] = {0};
    MYSQL_ROW row;
    MYSQL_RES *res_set = NULL;          //结果集指针

    long expire_time;
    char fileurl[1024];

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "redis connected error");
        ret = -1;
        goto failed;
    }

    //-------------------------1.检查code是否存在redis-------------------------
    ret2 = rop_zset_exit(redis_conn, CODE_PUBLIC_ZSET, code);
    if(ret2 == 1) //存在,则返回Url
    {
        ret2 = rop_hash_get(redis_conn, CODE_URL_HASH, code, codeVal);                 //获取文件提取码的url
        if(ret2 == 1)
        {
            //开始截取size和url
            strplit(codeVal,'_',us,1);
            //获取返回结果
            strcpy(size,us[0]);
            strcpy(url,us[1]);
            LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "redis: get extra code[%s] : url[%s]\n",code,url);
            goto failed;
        }
    }

    //-------------------------2.操作mysql查看是否存在url-------------------------
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto failed;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //连表查询
    sprintf(sql_cmd, "select file_info.url,file_info.size,file_info.file_name,file_info.md5,file_code_list.create_time,file_code_list.effect_time from file_code_list,file_info where file_info.md5 == file_code_list.md5 and file_code_list.file_code = '%s'", code);


    if(mysql_query(conn,sql_cmd) != 0)      //进行数据查询
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql_query error");
        ret = -1;
        goto failed;
    }

    res_set = mysql_store_result(conn);     //生成结果集
    if(res_set == NULL)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql_store_result error");
        ret = -1;
        goto failed;
    }

    long line = mysql_num_rows(res_set);
    if(line == 0)
    {
        ret = -1;                           //没有查询到数据
        goto failed;
    }

    //可能存在多个数据，我们只需要去获取一行数据
    if((row = mysql_fetch_row(res_set)) != NULL)
    {
        strcpy(url,row[0]);
        strcpy(size,row[1]);
        strcpy(filename,row[2]);
        strcpy(md5,row[3]);
        strcpy(createtime,row[4]);
        strcpy(effecttime,row[5]);
    }

    //判断数据是否过期
    struct tm stm;  
    strptime(createtime, "%Y-%m-%d %H:%M:%S",&stm);
    long t = mktime(&stm);

    t += atoi(effecttime) * 24 *3600;

    //获取当前时间
    time_t now;
    now = time(NULL);

    if(t <= now)                                //过期,删除数据库数据，并返回-2
    {
        ret = -2;
        //删除操作
        sprintf(sql_cmd, "delete from file_code_list where code = '%s'", code);

        if(mysql_query(conn,sql_cmd) != 0)      //进行sql执行
        {
            LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "mysql_query error");
            ret = -1;
            goto failed;
        }
        goto failed;
    }

    //更新redis数据
    expire_time = t;
    sprintf(fileurl,"%s_%s",size,url);

    //开始设置新的提取码信息
    rop_zset_add(redis_conn, CODE_PUBLIC_ZSET, expire_time, code);    //注意：以过期时间作为score，进行过期检查
    //设置提取码和文件url
    rop_hash_set(redis_conn, CODE_URL_HASH, code, fileurl);           //由后台程序进行检查

failed:
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

    return ret;
}


/*
======================转存文件======================
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
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }

    mysql_query(conn, "set names utf8");

    //2.查看此用户，文件名和md5是否存在，如果存在说明此文件存在
    sprintf(sql_cmd, "select * from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);
    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    ret2 = process_result_one(conn, sql_cmd, NULL); //执行sql语句, 最后一个参数为NULL
    if(ret2 == 2)                                   //如果有结果，说明此用户已有此文件
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "%s[filename:%s, md5:%s] exists\n", user, filename, md5);
        ret = -2;                                   //返回-2错误码
        goto END;
    }

    //3.文件信息表，查找该文件的引用计数器，用于更新
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if(ret2 != 0)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    int count = atoi(tmp); 

    //4.修改file_info中的count字段，+1 （count 文件引用计数）
    sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", count+1, md5);
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //5.user_file_list插入一条文件数据
    sprintf(sql_cmd, "insert into user_file_list(user, md5, file_name, shared_status, pv) values ('%s', '%s', '%s', %d, %d)", user, md5, filename, 0, 0);
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
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
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
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


int main()
{
    char cmd[20];
    char user[USER_NAME_LEN];                       //用户名
    char token[TOKEN_LEN];                          //token
    char md5[MD5_LEN];                              //文件md5码
    char filename[FILE_NAME_LEN];                   //文件名字
    char code[10];                                  //提取码

    //读取数据库配置信息
    read_cfg();

    //阻塞等待用户连接
    while (FCGI_Accept() >= 0)
    {

        // 获取URL地址 "?" 后面的内容
        char *query = getenv("QUERY_STRING");

        //解析命令
        query_parse_key_value(query, "cmd", cmd, NULL);
        LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "cmd = %s\n", cmd);

        printf("Content-type: text/html\r\n\r\n");


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
            LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "len = 0, No data from standard input\n");
        }
        else
        {
            char buf[4*1024] = {0};
            int ret = 0,ret2=0;
            char url[1024] = {0};
            char size[30] = {0};

            ret = fread(buf, 1, len, stdin);                        //从标准输入(web服务器)读取内容
            if(ret == 0)
            {
                LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                continue;
            }

            LOG(DOWNLOAD_LOG_MODULE, DOWNLOAD_LOG_PROC, "buf = %s\n", buf);

            if (strcmp(cmd, "webdownload") == 0)
            {
                get_download_json_info(buf, user, token, md5, filename);                    
                ret = verify_token(user, token);        //验证登陆token，成功返回0，失败-1
                if(ret != 0)
                {
                    return_download_status("111","failed"); //token验证失败错误码
                    goto END;                           //跳过本次循环
                }

                //获取url信息
                ret = get_web_files_url_size(filename,md5,url,size);
                if(ret != 0)
                {
                    return_download_status("111","failed"); //token验证失败错误码
                    goto END;                           //跳过本次循环   
                }

                if(atoi(size) > 100 * 1024)
                {
                    return_download_status("112","big file, please download in client");
                }
                else
                {
                    return_download_status("110",url);
                }

            }
            else if(strcmp(cmd, "code") == 0)
            {
                get_code_json_info(buf, user, token,code);
                ret2 = verify_token(user, token);        //验证登陆token，成功返回0，失败-1

                if(ret != 0)
                {
                    return_download_status("111","failed"); //token验证失败错误码
                    goto END;                           //跳过本次循环   
                }

                //获取url信息
                ret = get_code_url(code,url,size,filename,md5);

                if(atoi(size) > 100 * 1024)
                {
                    if(ret2 != 0)
                    {
                        return_download_status("113","big file, please login in, and download in client");
                    }
                    else
                    {
                        //转存文件
                        ret = save_file(user,md5,filename);
                        if(ret == -1)
                        {
                            return_download_status("111","failed"); //下载和转存都出错
                        }
                        else
                        {
                            return_download_status("114","big file, save file in myfile, please download in client");

                        }
                    }
                }
                else
                {
                    return_download_status("110",url);
                }
            }
        }
END:
        //数据初始化
        memset(user,0,USER_NAME_LEN);
        memset(token,0,TOKEN_LEN);
        memset(md5,0,MD5_LEN);
        memset(filename,0,FILE_NAME_LEN);
        memset(code,0,10);
    }

    return 0;
}

