/*
*用于处理文件的大多数操作：分享、删除、文件pv字段的处理；注意这里没有实现对文件的下载操作，后面单独实现
*注意：对于Web，我们修改mysql足够，对于Client我们需要去写入redis
*/
//标准头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

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
#include "des.h"                                //非对称加密算法
#include "md5.h"                                //md5加密
#include "base64.h"                             //base64

#define 	DEALFILE_LOG_MODULE			"cgi"
#define 	DEALFILE_LOG_PROC			"dealfile"

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
    LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]", mysql_user, mysql_pwd, mysql_db);

    //读取redis配置信息
    get_redis_info(redis_ip,redis_port);
    LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "redis:[ip=%s,port=%s]\n", redis_ip, redis_port);
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
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //用户
    cJSON *child1 = cJSON_GetObjectItem(root, "user");
    if(NULL == child1)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    //LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "child1->valuestring = %s\n", child1->valuestring);
    strcpy(user, child1->valuestring); //拷贝内容

    //token
    cJSON *child4 = cJSON_GetObjectItem(root, "token");
    if(NULL == child4)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(token, child4->valuestring); //拷贝内容

    //文件md5码
    cJSON *child2 = cJSON_GetObjectItem(root, "md5");
    if(NULL == child2)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(md5, child2->valuestring); //拷贝内容

    //文件名字
    cJSON *child3 = cJSON_GetObjectItem(root, "filename");
    if(NULL == child3)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "cJSON_GetObjectItem err\n");
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
        printf("%s",out);                        //反馈数据给前端
        free(out);
    }
}

//======================开始分享用户自己文件======================
/*
ret = 0分享成功
ret = -1出错
ret = -2被人分享过了
*/
int share_file(char *user, char *md5, char *filename)
{
    /*
    a)先判断此文件是否已经分享，判断集合有没有这个文件，如果有，说明别人已经分享此文件，中断操作(redis操作)----使用到redis_keys.h中的字段
    b)如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)----使用数据库
    c)如果mysql有记录，而redis没有记录，说明redis没有保存此文件，redis保存此文件信息后，再中断操作(redis操作)----回加入redis
    d)如果此文件没有被分享，mysql保存一份持久化操作(mysql操作)----持久化
    e)redis集合中增加一个元素(redis操作)----redis操作，同c后半操作
    f)redis对应的hash也需要变化 (redis操作)
    */

    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    redisContext * redis_conn = NULL;
    char *out = NULL;
    char tmp[512] = {0};
    char fileid[1024] = {0};
    int ret2 = 0;

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "redis connected error");
        ret = -1;
        goto END;
    }

    //===1、先判断此文件是否已经分享，判断集合有没有这个文件，如果有，说明别人已经分享此文件，中断操作(redis操作)
    //文件标示，md5+文件名
    sprintf(fileid, "%s%s", md5, filename);

    ret2 = rop_zset_exit(redis_conn, FILE_PUBLIC_ZSET, fileid);
    if(ret2 == 1) //存在
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "redis:[%s] other has shared this file[%s]\n",fileid,filename);
        ret = -2;
        goto END;
    }

    //===2、如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
    //处理数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //查看此文件别人是否已经分享了
    sprintf(sql_cmd, "select id from share_file_list where md5 = '%s' and file_name = '%s'", md5, filename);
    //执行语句，获取一条结果
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句, 最后一个参数为NULL
    if(ret2 == 2) //说明有结果，别人已经分享此文件
    {
		//===3、如果mysql有记录，而redis没有记录，说明redis没有保存此文件，redis保存此文件信息后，再中断操作(redis操作)
        //redis保存此文件信息
        rop_zset_add(redis_conn, FILE_PUBLIC_ZSET, 0, fileid);
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "mysql: other has shared this file[%s]\n",filename);
        ret = -2;
        goto END;
    }

    //===4、如果此文件没有被分享，mysql保存一份持久化操作(mysql操作)

    //4.1先更新共享标志字段
    sprintf(sql_cmd, "update user_file_list set shared_status = 1 where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    //4.2分享文件的信息，额外保存在share_file_list保存列表
    sprintf(sql_cmd, "insert into share_file_list (user, md5, file_name, pv) values ('%s', '%s', '%s', %d)", user, md5, filename, 0);
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    //查询共享文件数量,设置一个共享用户share_user
    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", "share_user");
    int count = 0;
    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if(ret2 == 1) //没有记录
    {
        //插入记录
        sprintf(sql_cmd, "insert into user_file_count (user, count) values('%s', %d)", "share_user", 1);	//此处插入共享用户信息
    }
    else if(ret2 == 0)
    {
        //更新用户文件数量count字段
        count = atoi(tmp);
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count+1, "share_user");	//更新共享用户的文件信息
    }

    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    //===5、redis集合中增加一个元素(redis操作)，这里默认设置了score为0，标识下载量为0！！！===pv字段值为0
    rop_zset_add(redis_conn, FILE_PUBLIC_ZSET, 0, fileid);

    //===6、redis对应的hash也需要变化 (redis操作)
    //     fileid ------>  filename
    rop_hash_set(redis_conn, FILE_NAME_HASH, fileid, filename);

END:
    if(ret == 0)
    {
        return_file_status("010","success");
    }
    else if(ret == -1)
    {
        return_file_status("011","failed");
    }
    else if(ret == -2)
    {
        return_file_status("012","shared");
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

//======================开始处理删除请求======================
//从storage删除指定的文件，参数为文件id-----当下面删除del_file方法发现file_info的count引用字段为0则真正删除文件！！
int remove_file_from_storage(char *conf_filename,char *fileid)	//参考fdfs_delete_file.c
{
    ConnectionInfo *pTrackerServer;
    int result;

    log_init();
    g_log_context.log_level = LOG_ERR;
    ignore_signal_pipe();

    if ((result=fdfs_client_init(conf_filename)) != 0)
    {
        return result;
    }

    pTrackerServer = tracker_get_connection();
    if (pTrackerServer == NULL)
    {
        fdfs_client_destroy();
        return errno != 0 ? errno : ECONNREFUSED;
    }

    if ((result=storage_delete_file1(pTrackerServer, NULL, fileid)) != 0)
    {

		LOG("fastDFS", "delete_file", "delete file fail, error no: %d, error info: %s\n",
			result, STRERROR(result));
    }

    tracker_close_connection_ex(pTrackerServer, true);
    fdfs_client_destroy();

    return result;
}

//删除文件---注意：本人删除文件后，共享文件连带消失！！！所以需要处理MySQL和redis
int del_file(char *user, char *md5, char *filename)
{
    /*
    a)先判断此文件是否已经分享
    b)判断集合有没有这个文件，如果有，说明别人已经分享此文件(redis操作)
    c)如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
    d)如果mysql有记录，而redis没有记录，那么分享文件处理只需要处理mysql (mysql操作)
    e)如果redis有记录，mysql和redis都需要处理，删除相关记录
    */
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    redisContext * redis_conn = NULL;
    char *out = NULL;
    char tmp[512] = {0};
    char fileid[1024] = {0};
    int ret2 = 0;
    int count = 0;
    int share = 0;  										//共享状态
    int flag = 0; 											//标志redis是否有记录

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "redis connected error");
        ret = -1;
        goto END;
    }

    //连接mysql数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //文件标示，md5+文件名
    sprintf(fileid, "%s%s", md5, filename);

    //===1、先判断此文件是否已经分享，判断集合有没有这个文件，如果有，说明别人已经分享此文件
    ret2 = rop_zset_exit(redis_conn, FILE_PUBLIC_ZSET, fileid);
    if(ret2 == 1) 											//存在redis中
    {
        share = 1;  										//共享标志
        flag = 1;   										//redis有记录
    }
    else if(ret2 == 0) 										//不存在redis中，去查找mysql
    {
    	//===2、如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
        //查看该文件是否已经分享了
        sprintf(sql_cmd, "select shared_status from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);

        //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
        ret2 = process_result_one(conn, sql_cmd, tmp); 		//执行sql语句
        if(ret2 == 0)										//存在这个文件，现在查看是否共享，0未共享，1共享
        {
            share = atoi(tmp); 								//shared_status字段
        }
        else if(ret2 == -1)									//失败，不存在这个文件
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
            ret = -1;
            goto END;
        }
    }
    else//出错
    {
        ret = -1;
        goto END;
    }

    //说明此文件被分享，删除分享列表(share_file_list)的数据，注意，删除需要带上user字段，防止用户去删除别人的数据
    if(share == 1)
    {
        //===3、如果mysql有记录，删除相关分享记录 (mysql操作)
        sprintf(sql_cmd, "delete from share_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);

        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }

        //共享文件的数量-1,先查询共享文件数量
        sprintf(sql_cmd, "select count from user_file_count where user = '%s'", "share_user");

        //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
        ret2 = process_result_one(conn, sql_cmd, tmp); 		//执行sql语句
        if(ret2 != 0)
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
            ret = -1;
            goto END;
        }

        count = atoi(tmp);
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count-1, "share_user");
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }

        //===4、如果redis有记录，redis需要处理，删除相关记录
        if(1 == flag)
        {
            //有序集合删除指定成员
            rop_zset_zrem(redis_conn, FILE_PUBLIC_ZSET, fileid);

            //从hash移除相应记录
            rop_hash_del(redis_conn, FILE_NAME_HASH, fileid);
        }

    }

    //查询用户文件数量，用户文件数量-1
    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
    ret2 = process_result_one(conn, sql_cmd, tmp); 		//执行sql语句
    if(ret2 != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    count = atoi(tmp);
    if(count == 1)	//删除用户文件数量表对应的数据,或者更新为0
    {
        sprintf(sql_cmd, "delete from user_file_count where user = '%s'", user);
    }
    else
    {
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count-1, user);
    }

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    //删除用户文件列表数据
    sprintf(sql_cmd, "delete from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    //文件信息表(file_info)的文件引用计数count，减去1
    //查看该文件文件引用计数！！！重点：决定了是否去fdfs系统中删除文件
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    ret2 = process_result_one(conn, sql_cmd, tmp); 		//执行sql语句
    if(ret2 == 0)
    {
        count = atoi(tmp); 								//count字段
    }
    else
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    count--; 											//减一
    if(count != 0)										//如果减少引用之后，数量不为0，我们修改数据库字段即可，不需要去删除文件
    {
	    sprintf(sql_cmd, "update file_info set count=%d where md5 = '%s'", count, md5);
	    if (mysql_query(conn, sql_cmd) != 0)
	    {
	        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
	        ret = -1;
	        goto END;
	    }
    }
    else 												//说明没有用户引用此文件，需要在storage删除此文件
    {
        //查询文件的id，后面删除需要用到这个字段！！！
        sprintf(sql_cmd, "select file_id from file_info where md5 = '%s'", md5);
        ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
        if(ret2 != 0)
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
            ret = -1;
            goto END;
        }

        //删除文件信息表中该文件的信息
        sprintf(sql_cmd, "delete from file_info where md5 = '%s'", md5);
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }

        //读取fdfs client 配置文件的路径
	    char fdfs_cli_conf_path[256] = {0};
	    get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);

        //从storage服务器删除此文件，参数为client配置文件和文件id
        ret2 = remove_file_from_storage(fdfs_cli_conf_path,tmp);
        if(ret2 != 0)
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "remove_file_from_storage err\n");
            ret = -1;
            goto END;
        }
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "remove_file_from_storage ret = %d\n", ret2);

    }


END:
    if(ret == 0)
    {
        return_file_status("013","success");
    }
    else
    {
        return_file_status("014","failed");
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


//======================开始处理下载完成请求--修改pv字段======================
int pv_file(char *user, char *md5, char *filename)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    char *out = NULL;
    char tmp[512] = {0};
    int ret2 = 0;
    int pv = 0;

    //连接数据库
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //查看该文件的pv字段
    sprintf(sql_cmd, "select pv from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);
    ret2 = process_result_one(conn, sql_cmd, tmp); 	//执行sql语句
    if(ret2 == 0)
    {
        pv = atoi(tmp); 							//pv字段
    }
    else
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s]\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //更新该文件pv字段，+1
    sprintf(sql_cmd, "update user_file_list set pv = %d where user = '%s' and md5 = '%s' and file_name = '%s'", pv+1, user, md5, filename);
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

END:
    if(ret == 0)
    {
        return_file_status("016","success");
    }
    else
    {
        return_file_status("017","failed");
    }

    if(conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}

//======================开始为用户文件生成提取码======================
int gen_extra_code(char *user,char *code)
{
    int ret = 0;
    int sum = 0;
    //开始生成token
    char randCode[62] = {
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
        'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
        '0','1','2','3','4','5','6','7','8','9'
    };
    int rand_num[4] = {0};
    int i = 0;

    LOG(DEALFILE_LOG_MODULE,DEALFILE_LOG_PROC,"gen_extra_code start\n");

    srand((unsigned int)time(NULL));
    for(;i < 4; i++){
        rand_num[i] = rand()%1000;          //生成随机数
    }

    char tmp[1024] = {0};
    sprintf(tmp,"%s%d%d%d%d",user,rand_num[0],rand_num[1],rand_num[2],rand_num[3]);

    //进行加密
    char enc_tmp[1024*2] = {0};
    int enc_len = 0;
    //将des加密后的数据存放到enc_tmp中
    ret = DesEnc((unsigned char*)tmp,strlen(tmp),(unsigned char*)enc_tmp,&enc_len); 
    if(ret != 0)
    {
        LOG(DEALFILE_LOG_MODULE,DEALFILE_LOG_PROC,"DesEnc error\n");
        ret = -1;
        goto failed;
    }

    //进行base64编码，会增大约33%
    char base64[1024*3] = {0};
    base64_encode((const unsigned char*)enc_tmp,enc_len,base64);
    LOG(DEALFILE_LOG_MODULE,DEALFILE_LOG_PROC,"base64 = %s\n",base64);

    //进行md5加密
    MD5_CTX md5;
    MD5Init(&md5);
    unsigned char decrypt[16];              //md5加密后长度为128位，16字节
    MD5Update(&md5,(unsigned char*)base64,strlen(base64));  //对base64数据进行加密
    MD5Final(&md5,decrypt);

    //开始将MD5值转为6位随机字符串a-zA-Z0-9
    for(i=0;i<16;i+=2)
    {
        sum = decrypt[i]*256 + decrypt[i+1];
        code[i/2] = randCode[sum%62];
    }
    LOG(DEALFILE_LOG_MODULE,DEALFILE_LOG_PROC,"random code = %s\n",code);

failed:
    return ret;
}


int set_extra_code(char *user, char *md5, char *filename,char *extra_code)
{
    /*
    a)先判断文件是否已经生成过提取码，如果没有则重新插入一个提取码
    b)如果有提取码（先获取旧的提取码），则更新提取码和提取码的生成时间和默认时效
    c)并且判断redis中是否存在旧的提取码信息，更新redis中提取码。提取码映射file url值
    */

    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    redisContext * redis_conn = NULL;
    char tmp[512] = {0};
    char code[30] = {0};
    char fileurl[1024] = {0};
    int ret2 = 0;

    //先获取时间信息---后面都会用到！！！
    time_t now,expire_time;
    char create_time[TIME_STRING_LEN];
    //获取当前时间
    now = time(NULL);
    //设置过期时间
    expire_time = now + 3600*24*7;                              //过期时间7天后

    //1.查询数据库，进行提取码的更新
    conn = mysql_conn(mysql_user, mysql_pwd, mysql_db);         //连接数据库
    if (conn == NULL)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "mysql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    //查看此文件是否已经有提取码了
    sprintf(sql_cmd, "select file_code from file_code_list where md5 = '%s' and file_name = '%s' and user = '%s'", md5, filename,user);
    //执行语句，获取一条结果
    ret2 = process_result_one(conn, sql_cmd, code);              //执行sql语句, 最后一个参数为NULL
    if(ret2 == 2)                                                //说明有结果，我们需要更新
    {
        strftime(create_time, TIME_STRING_LEN-1, "%Y-%m-%d %H:%M:%S", localtime(&now));

        sprintf(sql_cmd, "update file_code_list set file_code = '%s' and create_time = '%s' where md5 = '%s' and file_name = '%s' and user = '%s'", 
            extra_code,create_time,md5, filename,user);
    }
    else                                                        //没有对应的提取码，所以需要去插入
    {
        sprintf(sql_cmd, "insert file_code_list (user,md5,file_name,file_code) values ('%s','%s','%s','%s')",user,md5,filename,extra_code);
    }

    //2.执行sql语句，更新MySQL数据库
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "Failed to exec sql:[%s] error: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    //获取文件的url信息
    sprintf(sql_cmd, "select url from file_info where md5 = '%s'", md5);

    ret2 = process_result_one(conn, sql_cmd, fileurl);           //执行sql语句, 最后一个参数为NULL
    if(ret2 == 2)                                                //说明有结果，我们需要更新
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "mysql select url:[%s] for file:[%s]\n",fileurl,filename);
    }

    //3.将数据插入redis中去
    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
    if (redis_conn == NULL)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "redis connected error");
        ret = -1;
        goto END;
    }

    //先判断旧的提取码是否存在    
    ret2 = rop_zset_exit(redis_conn, CODE_PUBLIC_ZSET, code);
    if(ret2 == 1) //存在,则删除
    {
        rop_zset_zrem(redis_conn,CODE_PUBLIC_ZSET,code);
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "redis: delete old extra code [%s]\n",code);
    }

    //开始设置新的提取码信息
    rop_zset_add(redis_conn, CODE_PUBLIC_ZSET, expire_time, extra_code);    //注意：以过期时间作为score，进行过期检查
    //设置提取码和文件url
    rop_hash_set(redis_conn, CODE_URL_HASH, extra_code, fileurl);           //由后台程序进行检查

END:
    if(ret == 0)
    {
        return_file_status("010",extra_code);
    }
    else if(ret == -1)
    {
        return_file_status("011","failed");
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

//========================这里先简单实现，后面修改使用链表或者红黑树实现定时器任务！！！！
void *deal_redis_expire(void *arg)
{
    redisContext * redis_conn = NULL;
    char val[100][VALUES_ID_SIZE] = {0};
    int ret = 0;
    int get_nums = 0;
    int i=0;

    while(1)
    {
        redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
        if (redis_conn == NULL)
        {
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "redis connected error");
            ret = -1;
            goto END;
        }

        //开始检测是否有值过期
        //先获取时间信息
        time_t now,expire_time;
        char create_time[TIME_STRING_LEN];
        //获取当前时间
        now = time(NULL);

        ret = rop_zset_zrange_by_score(redis_conn,CODE_PUBLIC_ZSET,0,now,val,&get_nums,100);
        if(ret == 0)
        {
            //遍历数据，去删除hash表数据
            for(i = 0;i<get_nums;i++)
            {
                //删除hash中的url数据
                rop_hash_del(redis_conn,CODE_URL_HASH,val[i]);
            }
            //删除set数据
            rop_zset_delrange_by_score(redis_conn,CODE_PUBLIC_ZSET,0,now);
        }
        else if(ret == 1)
        {
            //遍历数据，去删除hash和set中的数据
            for(i = 0;i<get_nums;i++)
            {
                //删除hash中的url数据
                rop_hash_del(redis_conn,CODE_URL_HASH,val[i]);
                //删除set中的数据
                rop_zset_zrem(redis_conn,CODE_PUBLIC_ZSET,val[i]);
            }

            while((ret = rop_zset_zrange_by_score(redis_conn,CODE_PUBLIC_ZSET,0,now,val,&get_nums,100)) == 1)
            {
                //遍历数据，去删除hash和set中的数据
                for(i = 0;i<get_nums;i++)
                {
                    //删除hash中的url数据
                    rop_hash_del(redis_conn,CODE_URL_HASH,val[i]);
                    //删除set中的数据
                    rop_zset_zrem(redis_conn,CODE_PUBLIC_ZSET,val[i]);
                }
            }
                
            //遍历数据，去删除hash和set中的数据
            for(i = 0;i<get_nums;i++)
            {
                //删除hash中的url数据
                rop_hash_del(redis_conn,CODE_URL_HASH,val[i]);
                //删除set中的数据
                rop_zset_zrem(redis_conn,CODE_PUBLIC_ZSET,val[i]);
            }
        }
    END:
        get_nums = 0;
        if(redis_conn != NULL)
        {
            rop_disconnect(redis_conn);
        }
        sleep(600);                                 //10分钟检查一次
    }
}

int main()
{
    char cmd[20];
    int cmd_len = 0;
    char user[USER_NAME_LEN];   					//用户名
    char token[TOKEN_LEN];     	 					//token
    char md5[MD5_LEN];          					//文件md5码
    char filename[FILE_NAME_LEN]; 					//文件名字
    pthread_t tid;
    int ret = 0;

    //读取数据库和redis配置信息
    read_cfg();

    //开启线程，轮询redis数据，进行过期检测
    ret = pthread_create(&tid,NULL,deal_redis_expire,NULL);
    if(ret == -1)
    {
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "pthread_create error\n");
        return 0;
    }

    //阻塞等待用户连接
    while (FCGI_Accept() >= 0)
    {
         // 获取URL地址 "?" 后面的内容
        char *query = getenv("QUERY_STRING");

        //解析命令
        query_parse_key_value(query, "cmd", cmd, &cmd_len);
        LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "cmd = %s\n", cmd);

        char *contentLength = getenv("CONTENT_LENGTH");
        int len;

        printf("Content-type: text/html\r\n\r\n");

        if( contentLength == NULL )
        {
            len = 0;
        }
        else
        {
            len = atoi(contentLength); 				//字符串转整型
        }

        if (len <= 0)
        {
            printf("No data from standard input.<p>\n");
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "len = 0, No data from standard input\n");
        }
        else
        {
            char buf[4*1024] = {0};
            int ret = 0;
            ret = fread(buf, 1, len, stdin); 		//从标准输入(web服务器)读取内容
            if(ret == 0)
            {
                LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                continue;
            }

            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "buf = %s\n", buf);

            get_json_info(buf, user, token, md5, filename); //解析json信息
            LOG(DEALFILE_LOG_MODULE, DEALFILE_LOG_PROC, "user = %s, token = %s, md5 = %s, file_name = %s\n", user, token, md5, filename);

            ret = verify_token(user, token); 		//验证登陆token，成功返回0，失败-1
            if(ret != 0)
            {
		        return_file_status("111","failed");	//token验证失败错误码
                continue;							//跳过本次循环
            }

            if(strcmp(cmd, "share") == 0) 			//分享文件
            {
                share_file(user, md5, filename);
            }
            else if(strcmp(cmd, "del") == 0) 		//删除文件
            {
                del_file(user, md5, filename);
            }
            else if(strcmp(cmd, "pv") == 0) 		//文件下载标志处理
            {
                pv_file(user, md5, filename);
            }
            else if(strcmp(cmd,"gen") == 0)         //生成提取码
            {
                char extra_code[30] = {0};
                ret = gen_extra_code(user,extra_code);
                if(ret != 0)                        //提取码生成出错
                {
                    return_file_status("111","failed"); //token验证失败错误码
                    continue;
                }
                set_extra_code(user, md5, filename,extra_code);
            }
        }
    }

    pthread_join(tid,NULL);                         //等待子线程退出

    return 0;
}