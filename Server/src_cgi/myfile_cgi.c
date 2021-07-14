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
#include "redis_op.h"                           //处理redis模块
#include "util_cgi.h"                           //cgi后台通用接口
#include "cJSON.h"                              //JSON数据处理

#define     MYFILES_LOG_MODULE         "cgi"
#define     MYFILES_LOG_PROC           "myfiles"


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
    LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]", mysql_user, mysql_pwd, mysql_db);

    //读取redis配置信息
    get_redis_info(redis_ip,redis_port);
    LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "redis:[ip=%s,port=%s]\n", redis_ip, redis_port);
}


/*
======================返回后台文件数量状态，json数据返回给前端======================
*/
void return_file_status(char *status_num,char *message)
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


//======================解析的json包:这里分为两种方式，一个只解析count数量，一个解析一页的数据======================
//1.解析的json包, 登陆token，这里是针对count请求处理！！！
/*json数据如下
{
    "user":xxxx
    "token": "9e894efc0b2a898a82765d0a7f2c94cb",
}
*/
int get_count_json_info(char *buf, char *user, char *token)
{
    int ret = 0;

    //解析一个json字符串为cJSON对象
    cJSON * root = cJSON_Parse(buf);
    if(NULL == root)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //返回指定字符串对应的json对象
    cJSON *child1 = cJSON_GetObjectItem(root, "user");          //用户
    if(NULL == child1)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(user, child1->valuestring);                          //拷贝内容

    cJSON *child2 = cJSON_GetObjectItem(root, "token");         //登陆token
    if(NULL == child2)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(token, child2->valuestring);                         //拷贝内容

END:
    if(root != NULL)
    {
        cJSON_Delete(root);                                     //删除json对象
        root = NULL;
    }

    return ret;
}


//2.解析的json包，这里针对当前页的数据
/*json数据如下
{
    "user": "milo"
    "token": xxxx
    "start": 0
    "count": 10
}
*/
int get_fileslist_json_info(char *buf, char *user, char *token, int *p_start, int *p_count)
{
    int ret = 0;

    //解析json包
    //解析一个json字符串为cJSON对象
    cJSON * root = cJSON_Parse(buf);
    if(NULL == root)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //返回指定字符串对应的json对象
    cJSON *child1 = cJSON_GetObjectItem(root, "user");          //用户
    if(NULL == child1)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(user, child1->valuestring);                          //拷贝内容

    cJSON *child2 = cJSON_GetObjectItem(root, "token");         //token
    if(NULL == child2)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(token, child2->valuestring);                         //拷贝内容

    cJSON *child3 = cJSON_GetObjectItem(root, "start");         //文件起点
    if(NULL == child3)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    *p_start = child3->valueint;

    cJSON *child4 = cJSON_GetObjectItem(root, "count");         //文件请求个数
    if(NULL == child4)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    *p_count = child4->valueint;

END:
    if(root != NULL)
    {
        cJSON_Delete(root);                                     //删除json对象
        root = NULL;
    }

    return ret;
}

//======================获取用户文件个数======================
int get_user_files_count(char *user, char *count)
{
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    int ret = 0;

    //连接数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    sprintf(sql_cmd, "select count from user_file_count where user='%s'", user);

    //获取了用户所拥有的文件数量
    int ret2 = process_result_one(conn, sql_cmd, count); 
    if(ret2 != 0)
    {
        ret = -1;
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        goto END;
    }

END:
    if(conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}


//获取用户文件列表
//获取用户文件信息 127.0.0.1:80/myfiles&cmd=normal 正常按照文件上传时间
//按下载量升序 127.0.0.1:80/myfiles?cmd=pvasc      这里按照文件下载量的升序
//按下载量降序127.0.0.1:80/myfiles?cmd=pvdesc      这里按照文件下载量的降序
int get_user_filelist(char *cmd, char *user, int start, int count)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    cJSON *root = NULL;
    cJSON *array =NULL;
    char *out = NULL;
    char *out2 = NULL;
    MYSQL_RES *res_set = NULL;

    //连接数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //多表指定行范围查询
    if(strcmp(cmd, "normal") == 0)                      //获取用户文件信息
    {
        sprintf(sql_cmd, "select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, user_file_list where user = '%s' and file_info.md5 = user_file_list.md5 limit %d, %d", user, start, count);
    }
    else if(strcmp(cmd, "pvasc") == 0)                  //按下载量升序
    {
        sprintf(sql_cmd, "select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, user_file_list where user = '%s' and file_info.md5 = user_file_list.md5  order by pv asc limit %d, %d", user, start, count);
    }
    else if(strcmp(cmd, "pvdesc") == 0)                 //按下载量降序
    {
        sprintf(sql_cmd, "select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, user_file_list where user = '%s' and file_info.md5 = user_file_list.md5 order by pv desc limit %d, %d", user, start, count);
    }

    LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "Execute sql:[%s]....\n", sql_cmd);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    res_set = mysql_store_result(conn);                 //获取结果集，之后进行遍历，添加到返回的json中去
    if (res_set == NULL)
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "smysql_store_result error: %s!\n", mysql_error(conn));
        ret = -1;
        goto END;
    }

    ulong line = mysql_num_rows(res_set);               //mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    if (line == 0)                                      //没有数据
    {
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "mysql_num_rows(res_set) failed：%s\n", mysql_error(conn));
        ret = -1;
        goto END;
    }

    MYSQL_ROW row;                                      //用于遍历每一个数据

    root = cJSON_CreateObject();                        //创建json对象
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
        int column_index = 1;                           //避开了id信息，不需要！！！

        //注意：下面字段需要进行对应我们数据库设计的字段顺序！！！
        if(row[column_index] != NULL)                   //user   文件所属用户
        {   
            cJSON_AddStringToObject(item, "user", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)                  //md5 文件md5
        {
            cJSON_AddStringToObject(item, "md5", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)                  //createtime 文件创建时间
        {
            cJSON_AddStringToObject(item, "create_time", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)                 //filename 文件名字
        {
            cJSON_AddStringToObject(item, "file_name", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)                 //shared_status 共享状态, 0为没有共享， 1为共享
        {
            cJSON_AddNumberToObject(item, "share_status", atoi( row[column_index] ));
        }

        column_index++;
        if(row[column_index] != NULL)                 //pv 文件下载量，默认值为0，下载一次加1
        {
            cJSON_AddNumberToObject(item, "pv", atol( row[column_index] ));
        }

        column_index++;
        if(row[column_index] != NULL)                 //url 文件url
        {
            cJSON_AddStringToObject(item, "url", row[column_index]);
        }

        column_index++;
        if(row[column_index] != NULL)                 //size 文件大小, 以字节为单位
        {
            cJSON_AddNumberToObject(item, "size", atol( row[column_index] ));
        }

        column_index++;
        if(row[column_index] != NULL)                 //type 文件类型： png, zip, mp4……
        {
            cJSON_AddStringToObject(item, "type", row[column_index]);
        }

        cJSON_AddItemToArray(array, item);            //最后将结果加入json中
    }

    cJSON_AddItemToObject(root, "files", array);      //json创建完毕！！！

    out = cJSON_Print(root);                          //进行格式化输出！！

    LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "%s\n", out);

END:
    if(ret == 0)
    {
        printf("%s", out);                            //给前端反馈文件列表信息
    }
    else
    {   
        return_file_status("015","failed");           //给前端反馈错误码
    }

    if(res_set != NULL)
    {
        mysql_free_result(res_set);                  //完成所有对数据的操作后，调用mysql_free_result来善后处理
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

int main()
{
    //count 获取用户文件个数
    //display 获取用户文件信息，展示到前端
    char cmd[20];
    int cmdlen = 0;
    char user[USER_NAME_LEN];
    char token[TOKEN_LEN];

     //读取数据库配置信息
    read_cfg();

    //阻塞等待用户连接
    while (FCGI_Accept() >= 0)
    {

        // 获取URL地址 "?" 后面的内容
        char *query = getenv("QUERY_STRING");

        //解析命令
        query_parse_key_value(query, "cmd", cmd, &cmdlen);
        LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "cmd = %s\n", cmd);

        char *contentLength = getenv("CONTENT_LENGTH");
        int len;

        printf("Content-type: text/html\r\n\r\n");

        if( contentLength == NULL )
        {
            len = 0;
        }
        else
        {
            len = atoi(contentLength); //字符串转整型
        }

        if (len <= 0)
        {
            printf("No data from standard input.<p>\n");
            LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "len = 0, No data from standard input\n");
        }
        else
        {
            char buf[4*1024] = {0};
            int ret = 0;
            ret = fread(buf, 1, len, stdin);                            //从标准输入(web服务器)读取内容
            if(ret == 0)
            {
                LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                continue;
            }

            LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "buf = %s\n", buf);

            if (strcmp(cmd, "count") == 0)                              //count 获取用户文件个数
            {
                char count[30] = {0};
                get_count_json_info(buf, user, token);                  //通过json包获取用户名, token

                ret = verify_token(user, token);                        //验证登陆token，成功返回0，失败-1
                if(ret != 0)
                {
                    return_file_status("111","failed");                 //token验证失败错误码
                    continue;                                           //跳过本次循环
                }

                ret = get_user_files_count(user, count);                //获取用户文件个数
                if(ret != 0)
                {
                    return_file_status("111","failed");                 //文件获取失败
                    continue;                                           //跳过本次循环
                }

                return_file_status("110",count);                        //返回文件数量

            }
            else                                                        //下面开始获取文件列表
            {
                //获取用户文件信息 127.0.0.1:80/myfiles&cmd=normal
                //按下载量升序 127.0.0.1:80/myfiles?cmd=pvasc
                //按下载量降序127.0.0.1:80/myfiles?cmd=pvdesc
                int start; //文件起点
                int count; //文件个数

                get_fileslist_json_info(buf, user, token, &start, &count); //通过json包获取信息
                LOG(MYFILES_LOG_MODULE, MYFILES_LOG_PROC, "user = %s, token = %s, start = %d, count = %d\n", user, token, start, count);

                
                ret = verify_token(user, token);                        //验证登陆token，成功返回0，失败-1
                if(ret == 0)
                {
                    get_user_filelist(cmd, user, start, count);        //获取用户文件列表
                }
                else
                {
                    return_file_status("111","failed");                 //token验证失败错误码
                }

            }

        }

    }

    return 0;
}
