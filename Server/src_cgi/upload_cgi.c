/*
*用于处理文件上传的CGI程序 ----  客户端未实现（使用分片多线程尝试）！！！
*/

//fastcgi头文件
#include "fcgi_config.h"
#include "fcgi_stdio.h"
//标准头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
//文件操作、进程操作头文件
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>								//包含进程的处理
#include <sys/wait.h>
//fastfds头文件
#include "fastdfs/tracker_types.h"				//一些定义存放在这里
#include "fastdfs/fdfs_client.h"
#include "fastcommon/logger.h"

//自定义头文件
#include "deal_mysql.h"							//处理数据库模块
#include "cfg.h"								//配置信息
#include "make_log.h"							//日志处理模块	
#include "redis_op.h"							//处理redis模块
#include "util_cgi.h" 							//cgi后台通用接口
#include "cJSON.h"								//JSON数据处理


#define UPLOAD_LOG_MODULE "cgi"
#define UPLOAD_LOG_PROC   "upload"

//mysql 数据库配置信息 用户名， 密码， 数据库名称,因为后面多个函数会用到这些信息与数据库交互，所以这里设置为全局变量
static char mysql_user[128] = {0};
static char mysql_pwd[128] = {0};
static char mysql_db[128] = {0};

int read_mysql_cfg()
{
	int ret = 0;
	ret = get_cfg_value(CFG_PATH, "mysql", "user", mysql_user);
	if(ret != 0)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to get mysql user info!\n");
		goto END;
	}

	ret = get_cfg_value(CFG_PATH, "mysql", "password", mysql_pwd);
	if(ret != 0)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to get mysql password info!\n");
		goto END;
	}

	ret = get_cfg_value(CFG_PATH, "mysql", "database", mysql_db);
	if(ret != 0)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to get mysql database info!\n");
		goto END;
	}
END:
	return ret;
}


/*
连接redis，校验token值
*/
int check_user_token(char *user, char *token)
{
	int ret = 0;
	redisContext *redis_conn = NULL;

	//redis服务器ip与端口
	char redis_ip[30] = {0};
	char redis_port[10] = {0};

	char tokenCache[TOKEN_LEN] = {0};			//token数据

	//读取redis配置信息
	get_redis_info(redis_ip,redis_port);
	LOG(UPLOAD_LOG_MODULE,UPLOAD_LOG_PROC,"redis:[ip=%s,port=%s]\n",redis_ip,redis_port);

	//连接redis数据库
	redis_conn = rop_connectdb_nopwd(redis_ip,redis_port);
	if(redis_conn == NULL)
	{
		LOG(UPLOAD_LOG_MODULE,UPLOAD_LOG_PROC,"redis connected error!\n");
		ret = -1;
		goto failed;
	}

	//获取token值
	ret = rop_get_string(redis_conn,user,tokenCache);
	if(ret != 0)
	{
		LOG(UPLOAD_LOG_MODULE,UPLOAD_LOG_PROC,"get user[%s] token failed!\n",user);
		ret = -1;
		goto failed;
	}

	if(memcmp(tokenCache,token,strlen(token)) != 0)
	{
		LOG(UPLOAD_LOG_MODULE,UPLOAD_LOG_PROC,"token check failed!\n");
		ret = -1;
		goto failed;
	}

failed:
	if(redis_conn != NULL)
	{
		rop_disconnect(redis_conn);
	}

	return ret;
}



/*
获取文件信息，返回部分数据中文件内容的首部
传入获取的部分数据buf,对buf进行解析，判断来自web，还是client端
从而获取文件名、文件大小，md5、用户名、用户token信息
//传入参数
char *boundary ： 边界符号
char *chunk ： 文件块
int len： 文件块中的数据长度
//输出参数		
int *size ： 文件大小
char *filename ：文件名
char *user ： 用户名
char *token ： token
char *md5 ： 文件md5
*/
char *getFileInfo(char *boundary,char *chunk,int len,
					int *size,char *filename,char *user,char *token,char *md5)
{
	if(boundary == NULL || chunk == NULL || len <= 0)
	{
		return NULL;
	}
	//开始读取第一行
	char *index = chunk + strlen(boundary) + 4;
	char *index_e = memstr(index,len-(index-chunk),"\r\n");
	char filesize[30] = {0};
	if(memstr(index,index_e - index,"extra"))
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Web data Comming ...\n");
        //开始解析
        index = index_e + 4;
        index_e = memstr(index,len-(index-chunk),"\r\n");

        //解析md5---------------
        char *dptr = memstr(index,index_e-index,"md5");
        if(dptr == NULL){
        	return NULL;
        }
        dptr += 7;
        char *delim = memstr(dptr,index_e-dptr,"\"");
        //拷贝md5
        strncpy(md5,dptr,delim-dptr);

        //解析user---------------
        dptr = memstr(index,index_e-index,"user");
        if(dptr == NULL){
        	return NULL;
        }
        dptr += 7;
        delim = memstr(dptr,index_e-dptr,"\"");
        //拷贝user
        strncpy(user,dptr,delim-dptr);

        //解析token---------------
        dptr = memstr(index,index_e-index,"token");
        if(dptr == NULL){
        	return NULL;
        }
        dptr += 8;
        delim = memstr(dptr,index_e-dptr,"\"");
        //拷贝token
        strncpy(token,dptr,delim-dptr);

        //解析filename---------------
        dptr = memstr(index,index_e-index,"filename");
        if(dptr == NULL){
        	return NULL;
        }
        dptr += 11;
        delim = memstr(dptr,index_e-dptr,"\"");
        //拷贝filename
        strncpy(filename,dptr,delim-dptr);

        //解析size---------------
        dptr = memstr(index,index_e-index,"size");
        if(dptr == NULL){
        	return NULL;
        }
        dptr += 6;
        delim = memstr(dptr,index_e-dptr,"}");
        //拷贝size
        strncpy(filesize,dptr,delim-dptr);
        *size = atoi(filesize);

        //向下推移，找到数据指针
        index = memstr(chunk,len,"Content-Type");
        index = memstr(index,len - (index - chunk),"\r\n");
        index += 4;
	}
	else
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Client data Comming ...\n");

	}

	return index;
}

int recv_save_file(char *chunk,int chunk_size,int datasize,char *filename,int filesize,char *boundary,char *exist_data,int exist_len)
{
	int fd = 0, ret = 0;
	fd = open(filename,O_CREAT|O_WRONLY, 0644);
	if(fd < 0)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Open file:%s failed!\n",filename);
        ret = -1;								//文件打开失败
        goto CLEAN;
	}

	ftruncate(fd,filesize);						//创建这个文件空间大小为size，后面只需要进行数据

	if(exist_data != NULL)						//先处理在文件块中的数据
	{
		if(exist_len < filesize)				//文件足够大
		{
			write(fd,exist_data,exist_len);
			filesize -= exist_len;
			datasize -= exist_len;
		}
		else 									//文件足够小
		{
			write(fd,exist_data,filesize);
			filesize -= filesize;
			datasize -= filesize;
			//进行校验
			memcpy((void *)chunk,(void *)(exist_data+filesize),strlen(boundary)+2);
			goto CHECK;
		}
	}
	//开始循环读取数据
	while(filesize - chunk_size > 0)			//文件数据足够一个块
	{
    	fread(chunk, 1, chunk_size, stdin); 	//从标准输入(web服务器)读取内容
		write(fd,chunk,chunk_size);				//写入文件

    	datasize -= chunk_size;
    	filesize -= chunk_size;
	}

	if(filesize > 0)							//读取不足的数据
	{
		fread(chunk,1,filesize,stdin);			//剩余数据
		write(fd,chunk,filesize);
		datasize -= filesize;
		filesize = 0;
	}

	//校验边界符
	fread(chunk,1,strlen(boundary) + 4,stdin);
CHECK:
	if(memcmp(chunk + 4,boundary,strlen(boundary)) != 0)	//校验不过
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "File:%s check failed!\n",filename);
		ret = -2;								//校验失败
	}
	datasize -= strlen(boundary) + 2;

CLEAN:
	//清理多余数据
	while(datasize - chunk_size > 0)
	{
		fread(chunk,1,chunk_size,stdin);
		datasize -= chunk_size;
	}
	if(datasize > 0)
	{
		fread(chunk,1,datasize,stdin);
	}


END:
	close(fd);
	return ret;
}

/*
参考fdfs client中提供的样例fdfs_upload_file.c
*/
int __upload2dstorage(char *filename,char *confinfo,char *fdfs_file_id)
{
	int result;
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];	//FDFS_GROUP_NAME_MAX_LEN定义在client头文件中，不需要我们再定义

	ConnectionInfo *pTrackerServer;
	int store_path_index;
	ConnectionInfo storageServer;

    log_init();
    g_log_context.log_level = LOG_ERR;
    ignore_signal_pipe();

    //加载配置文件，进行初始化
    if ((result=fdfs_client_init(confinfo)) != 0)
    {
        return result;
    }

    //通过配置文件，连接到tracker,并得到一个可以访问trackerServer的句柄
    pTrackerServer = tracker_get_connection();
    if (pTrackerServer == NULL)
    {
            fdfs_client_destroy();
            return errno != 0 ? errno : ECONNREFUSED;
    }

    *group_name = '\0';

    //通过tracker句柄，获取一个可以访问的storage句柄
    if((result=tracker_query_storage_store(pTrackerServer,
    							&storageServer,group_name,&store_path_index)) != 0)
    {
    	fdfs_client_destroy();
		LOG("fastDFS", "upload_file", "tracker_query_storage fail, error no: %d, error info: %s\n",
			result, STRERROR(result));

		return result;
    }

    //通过得到的storage句柄，去上传本地文件
    result = storage_upload_by_filename1(pTrackerServer, \
                &storageServer, store_path_index, \
                filename, NULL, \
                NULL, 0, group_name, fdfs_file_id);
    if(result == 0)
    {
		LOG("fastDFS", "upload_file", "Success upload to fdfs system, fileID:[%s]\n",fdfs_file_id);
    }
    else
    {
		LOG("fastDFS", "upload_file", "Failed upload to fdfs system\n");
    }

    //断开连接，释放资源
    tracker_close_connection_ex(pTrackerServer, true);
    fdfs_client_destroy();

	return result;
}

/*
创建子进程，上传filename文件到fdfs系统中去，获取fdfs返回的fdfs_file_id
*/
int upload_to_dstorage(char *filename,char *fdfs_file_id)
{
	int ret = 0;

	pid_t pid;									//进程号
	int fd[2];									//管道通信,一个用于父进程写入子进程读取，一个用于子进程写入父进程读取

	if(pipe(fd) < 0)							//创建无名管道
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "PIPE create failed!\n");
        ret = -1;
        goto END;
	}

	//创建进程
	pid = fork();								//创建进程，父进程获取的pid > 0为子进程号，子进程的为0
	if(pid < 0)									//进程创建失败
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Fork failed!\n");
		ret = -1;
		goto END;
	}

	if(pid == 0)								//子进程,只需要写入fdfs系统返回的数据给父进程
	{
		//关闭管道fd[0],不进行读
		close(fd[0]);

		//开始向fdfs系统中写入数据
		char fdfs_cli_conf_path[FDFS_CLI_CONF_PATH_LEN] = {0};
		get_cfg_value(CFG_PATH,"dfs_path","client",fdfs_cli_conf_path);

		//开始调用库函数，上传数据
		ret = __upload2dstorage(filename,fdfs_cli_conf_path,fdfs_file_id);
		if(ret != 0)
		{
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to upload file to fdfs system!\n");
            write(fd[1],"",0);
		}
		else
		{
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "upload_to_dstorage Success to upload file to fdfs system!\n");
			write(fd[1],fdfs_file_id,TEMP_BUF_MAX_LEN);
		}

		close(fd[1]);
	}
	else 										//父进程，获取子进程返回的fdfs系统存放的文件内容
	{
		//关闭写端
		close(fd[1]);

		//从管道中去读取数据,用于返回
		read(fd[0],fdfs_file_id,TEMP_BUF_MAX_LEN);

		//去除空白
		trim_space(fdfs_file_id);
		if(strlen(fdfs_file_id) == 0)
		{
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to upload file to fdfs system!\n");
            ret = -1;
		}
		else
		{
	        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Success to upload file to fdfs system! fdfs id:%s\n",fdfs_file_id);
		}

        wait(NULL);								//等待子进程退出
		close(fd[0]);
	}

END:
	return ret;
}

/*
模拟fdfs命令中的fdfs_file_info，来实现获取文件的完整信息
*/
int __fdfs_file_info(char *fdfs_file_id,char *confinfo,char *fdfs_file_stat,char *fdfs_file_host_name)
{
	int result;
	FDFSFileInfo file_info;

    log_init();
    g_log_context.log_level = LOG_ERR;
    ignore_signal_pipe();

	LOG("fastDFS", "upload_file", "conf:%s  fileid:%s\n",confinfo,fdfs_file_id);

    //通过配置信息，初始化客户端状态
    if ((result=fdfs_client_init(confinfo)) != 0)
    {
        return result;
    }

    //开始通过file id去fdfs系统中获取文件信息
    memset(&file_info, 0, sizeof(file_info));
    result = fdfs_get_file_info_ex1(fdfs_file_id, true, &file_info);
    if (result != 0)
    {
		LOG("fastDFS", "upload_file", "query file info fail, error no: %d, error info: %s\n",
			result, STRERROR(result));
		goto END;
    }

    //开始获取文件信息
    switch (file_info.file_type)							//获取文件状态信息
    {
        case FDFS_FILE_TYPE_NORMAL:
            strcpy(fdfs_file_stat,"normal");
            break;
        case FDFS_FILE_TYPE_SLAVE:
            strcpy(fdfs_file_stat,"slave");
            break;
        case FDFS_FILE_TYPE_APPENDER:
            strcpy(fdfs_file_stat,"appender");
            break;
        default:
            strcpy(fdfs_file_stat,"unkown");
            break;
    }    

    strcpy(fdfs_file_host_name,file_info.source_ip_addr);	//获取文件所在storage的ip信息

	LOG("fastDFS", "upload_file", "File status:%s, File Storage in%s %s\n",fdfs_file_stat,file_info.source_ip_addr,fdfs_file_host_name);
END:
    //关闭，释放连接
    tracker_close_all_connections();
    fdfs_client_destroy();    
    return 0;
}

/*
获取在fdfs系统中的文件的完整url信息，返回在fdfs_file_url中
*/
int get_file_url(char *fdfs_file_id,char *fdfs_file_url)
{
	int ret = 0;

	char fdfs_file_stat[TEMP_BUF_MAX_LEN] = {0};	//用于获取文件的状态信息，比如file type: normal正常
	char fdfs_file_host_name[HOST_NAME_LEN] = {0};	//用于获取storage主机的Ip信息

	pid_t pid;

	pid = fork();									//创建进程
	if(pid < 0)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Fork failed!\n");
        ret = -1;
        goto END;		
	}

	if(pid == 0)									//子进程，去获取文件信息
	{
		//获取client配置信息
		char fdfs_cli_conf_path[FDFS_CLI_CONF_PATH_LEN] = {0};
		get_cfg_value(CFG_PATH,"dfs_path","client",fdfs_cli_conf_path);

		//开始去获取文件信息！
		ret = __fdfs_file_info(fdfs_file_id,fdfs_cli_conf_path,
								fdfs_file_stat,fdfs_file_host_name);
		if(ret != 0)
		{
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to get file info in fdfs system!\n");
		}
		else 										//信息获取成功
		{
			//开始拼接
			//先去获取端口信息
			char storage_web_server_port[20] = {0};
        	get_cfg_value(CFG_PATH, "storage_web_server", "port", storage_web_server_port);

			sprintf(fdfs_file_url,"http://%s:%s/%s",fdfs_file_host_name,storage_web_server_port,fdfs_file_id);
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "url %s!\n",fdfs_file_url);
		}
	}
	else 											//父进程，等待子进程退出
	{
        wait(NULL);								//等待子进程退出
	}


END:
	return ret;
}


/*
校验文件md5是否存在数据库中
1.输入md5文件校验值,判断文件是否在fdfs中存在 ret = -2
2.输入user用户名，判断用户是否已经上传了这个文件，返回文件名到filename ret = -3
ret = -1，表示连接数据库出错；
ret = 0，表示fdfs系统不存在这个文件，可以直接上传
*/
int check_filemd5_from_mysql(char *md5,char *user,char *filename)
{
	int ret = 0;
	MYSQL *conn = NULL;							//数据库连接句柄

	char sql_cmd[SQL_MAX_LEN] = {0};			//数据库语句

	conn = mysql_conn(mysql_user,mysql_pwd,mysql_db);	//连接到数据库
	if(conn == NULL)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Mysql Connection error!\n");
        ret = -1;
        goto END;
	}

	mysql_query(conn,"set names utf8");			//设置数据库编码


    sprintf(sql_cmd,"select id from file_info where md5 ='%s'",md5);
    int ret2 = 0;
    char temp[512] = {0};						//记录查询结果，id

    //查询一条数据，返回0查询到数据，返回1表示没有数据
    ret2 = process_result_one(conn,sql_cmd,temp);
    if(ret2 == 1)								//没有数据
    {
    	ret = 0;								//原始数据没有，后面正常流程，先上传到fdfs系统，然后加入数据到mysql
    }
    else if(ret2 == 0)							//查询到count数据，存放在temp中
    {
    	//查询用户是否已经用于这个文件了
    	sprintf(sql_cmd,"select file_name from user_file_list where md5 = '%s' and user = '%s'",md5,user);
    	ret2 = process_result_one(conn,sql_cmd,filename);
    	if(ret2 == 1)
    	{
	    	ret = -2;							//原始数据存在，不需要再上传文件，并且我们修改数据库，只需要修改部分表
    	}
    	else if(ret2 == 0)						//查询到filename数据，存放在filename中
    	{
    		ret = -3;
    	}
    }

END:
	if(conn != NULL)
	{
		mysql_close(conn);						//断开数据库连接
	}

	return ret;
}

int __store_fileinfo_to_mysql(char *user,char *filename,char *md5)
{
	int ret = 0;
	MYSQL *conn = NULL;							//数据库连接句柄

	char suffix[SUFFIX_LEN] = {0};				//文件后缀
	char sql_cmd[SQL_MAX_LEN] = {0};			//数据库语句

	conn = mysql_conn(mysql_user,mysql_pwd,mysql_db);	//连接到数据库
	if(conn == NULL)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Mysql Connection error!\n");
        ret = -1;
        goto END;
	}

	mysql_query(conn,"set names utf8");			//设置数据库编码

	//------------开始对 用户-文件信息表 进行插入操作-------
    sprintf(sql_cmd,"insert into user_file_list(user,md5,file_name,shared_status, pv) values ('%s','%s','%s',%d,%d)",
    	user,md5,filename,0,0);

	if(mysql_query(conn,sql_cmd) != 0) 			//执行sql语句
	{
	    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to exec sql:[%s]\n",sql_cmd);
        ret = -1;
        goto END;
	}
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Success to exec sql:[%s]\n",sql_cmd);

	//------------开始对 用户-文件数量表 进行插入操作-------
    //先去查询是否用户之前存在文件
    sprintf(sql_cmd,"select count from user_file_count where user ='%s'",user);
    int ret2 = 0;
    char temp[512] = {0};						//记录查询结果，count
    int count = 0;

    //查询一条数据，返回0查询到数据，返回1表示没有数据
    ret2 = process_result_one(conn,sql_cmd,temp);
    if(ret2 == 1)								//没有数据据
    {
    	sprintf(sql_cmd,"insert into user_file_count (user,count) values ('%s',%d)",user,1);
    }
    else if(ret2 == 0)							//查询到count数据，存放在temp中
    {
    	count = atoi(temp);
    	sprintf(sql_cmd,"update user_file_count set count = %d where user = '%s'",count+1,user);
    }

    //执行sql语句
    if(mysql_query(conn,sql_cmd) != 0)
    {
	    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to exec sql:[%s]\n",sql_cmd);
    	ret = -1;
    	goto END;
    }
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Success to exec sql:[%s]\n",sql_cmd);

END:
	if(conn != NULL)
	{
		mysql_close(conn);						//断开数据库连接
	}

	return ret;
}

//------未开启事务
int store_fileinfo_to_mysql(char *user,char *filename,char *md5,int size,
									char *fdfs_file_id,char *fdfs_file_url)
{
	int ret = 0;
	MYSQL *conn = NULL;							//数据库连接句柄

	char suffix[SUFFIX_LEN] = {0};				//文件后缀
	char sql_cmd[SQL_MAX_LEN] = {0};			//数据库语句

	conn = mysql_conn(mysql_user,mysql_pwd,mysql_db);	//连接到数据库
	if(conn == NULL)
	{
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Mysql Connection error!\n");
        ret = -1;
        goto END;
	}

	mysql_query(conn,"set names utf8");			//设置数据库编码

	get_file_suffix(filename,suffix);			//获取文件后缀

	//------------开始对 文件信息表 进行插入操作-------
	sprintf(sql_cmd,"insert into file_info (md5, file_id, url, size, type, count) values ('%s','%s','%s',%d,'%s',%d)",
		md5,fdfs_file_id,fdfs_file_url,size,suffix,1);

	if(mysql_query(conn,sql_cmd) != 0) 			//执行sql语句
	{
	    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to exec sql:[%s]\n",sql_cmd);
        ret = -1;
        goto END;
	}

    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Success to exec sql:[%s]\n",sql_cmd);

	//------------开始对 用户-文件信息表 进行插入操作-------
    sprintf(sql_cmd,"insert into user_file_list(user,md5,file_name,shared_status, pv) values ('%s','%s','%s',%d,%d)",
    	user,md5,filename,0,0);

	if(mysql_query(conn,sql_cmd) != 0) 			//执行sql语句
	{
	    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to exec sql:[%s]\n",sql_cmd);
        ret = -1;
        goto END;
	}
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Success to exec sql:[%s]\n",sql_cmd);

	//------------开始对 用户-文件数量表 进行插入操作-------
    //先去查询是否用户之前存在文件
    sprintf(sql_cmd,"select count from user_file_count where user ='%s'",user);
    int ret2 = 0;
    char temp[512] = {0};						//记录查询结果，count
    int count = 0;

    //查询一条数据，返回0查询到数据，返回1表示没有数据
    ret2 = process_result_one(conn,sql_cmd,temp);
    if(ret2 == 1)								//没有数据据
    {
    	sprintf(sql_cmd,"insert into user_file_count (user,count) values ('%s',%d)",user,1);
    }
    else if(ret2 == 0)							//查询到count数据，存放在temp中
    {
    	count = atoi(temp);
    	sprintf(sql_cmd,"update user_file_count set count = %d where user = '%s'",count+1,user);
    }

    //执行sql语句
    if(mysql_query(conn,sql_cmd) != 0)
    {
	    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to exec sql:[%s]\n",sql_cmd);
    	ret = -1;
    	goto END;
    }
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Success to exec sql:[%s]\n",sql_cmd);

END:
	if(conn != NULL)
	{
		mysql_close(conn);						//断开数据库连接
	}

	return ret;
}

/*
返回后台状态，json数据返回给前端
*/
void return_upload_status(char *status_num,char *message)
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

int main()
{
	char filename[FILE_NAME_LEN] = {0};			//文件名---上传的文件
	char userfile[FILE_NAME_LEN] = {0};			//文件名---用户已经拥有
	char md5[MD5_LEN] = {0};					//文件md5值
	int size;									//记录文件大小

	char user[USER_NAME_LEN] = {0};				//用户名
	char token[TOKEN_LEN] = {0};				//token数据

	char fdfs_file_id[TEMP_BUF_MAX_LEN] = {0};	//fdfs中存放的文件id
	char fdfs_file_url[FILE_URL_LEN] = {0};		//文件所存放的storage的host name

	char boundary[TEMP_BUF_MAX_LEN] = {0};		//分界线

	char chunk[CHUNK_MAX_LEN] = {0};			//每次读取数据块

	//读取数据库配置信息，后面函数调用需要使用
	read_mysql_cfg();

	while(FCGI_Accept() >= 0)
	{
		//分别获取客户端数据长度和边界线
		char *contentLength = getenv("CONTENT_LENGTH");
		char *contentType = getenv("CONTENT_TYPE");	

		int len;								//客户端传递的所有数据大小
		int ret = 0;
		printf("Content-type: text/html\r\n\r\n");

		if(contentLength != NULL)
		{
			len = atoi(contentLength);
		}
		else
		{
            printf("No data from standard input\n");
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "len = 0, No data from standard input\n");
            continue;
		}

		if(contentType != NULL)					//获取边界线
		{
			char *bdy = strstr(contentType,"=");
			strcpy(boundary,(char*)(bdy+1));	//边界线
		}

		//先读取第一块数据，解析我们需要的数据
		int tlen = CHUNK_MAX_LEN > len ? len : CHUNK_MAX_LEN;
		int ret2 = fread(chunk,1,tlen,stdin);	//读取客户端数据

		char *exist_data = getFileInfo(boundary,chunk,tlen,
			&size,filename,user,token,md5);

		if(size <= 0)
		{
            printf("No data from standard input\n");
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "len = 0, No data from standard input\n");
            continue;
		}

		//校验token信息
		ret = check_user_token(user,token);
		if(ret != 0)
		{
			printf("user token check failed!\n");
			goto failed;
		}

		//保存文件数据
		ret = recv_save_file(chunk,CHUNK_MAX_LEN,len,filename,size,boundary,exist_data,tlen - (exist_data - chunk));
		if(ret != 0)
		{
			printf("file upload failed!\n");
			goto failed;
		}
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Success upload file:%s,filesize:%d,md5:%s. by [%s]\n",filename,size,md5,user);

        //校验文件是否已经存在
        ret = check_filemd5_from_mysql(md5,user,userfile);
        if(ret != 0)
        {
        	if(ret == -1)								//数据库连接失败
        	{
		        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed to check file md5!\n");
		        goto failed;
        	}
        	else if(ret == -2)							//原始文件存在，但是用户没有这个文件
        	{
		        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Origin file is exists， join to user:[%s]!\n",user);
		        unlink(filename);
		        __store_fileinfo_to_mysql(user,filename,md5);
		        ret = 0;
		        goto failed;
        	}
        	else if(ret == -3)							//用户已经拥有这个文件了
        	{
		        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "User own this file[%s]:[%s]!\n",filename,userfile);
		        unlink(filename);
		        ret = 0;
		        goto failed;
        	}
        }


		//开始上传文件到分布式系统中去！并且获取文件在fdfs系统中的file id,例如：group1/M00/00/00/wKgBgWDpBzyAX6qLAAGgOspRecY20.docx
		ret = upload_to_dstorage(filename,fdfs_file_id);
		if(ret != 0)
		{
	        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed upload file:%s to fdfs system!\n",filename);
			goto failed;
		}

		//删除本地文件临时文件
		unlink(filename);

		//去获取文件存储在分布式系统中的完整url
		ret = get_file_url(fdfs_file_id,fdfs_file_url);
		if(ret != 0)
		{
	        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "Failed get file:%s info in fdfs system!\n",filename);
			goto failed;
		}

		//存储数据以上数据到数据库中
		ret = store_fileinfo_to_mysql(user,filename,md5,size,fdfs_file_id,fdfs_file_url);
		if(ret != 0)
		{
			ret = -1;
			goto failed;
		}
failed:
		if(ret == 0)							//上传成功
		{
			if(strlen(userfile) > 0)
			{
				return_upload_status("007",userfile);
			}
			else
			{
				return_upload_status("008","success");
			}
		}
		else									//上传失败
		{
			return_upload_status("009","error");
		}

		//数据初始化
		memset(filename,0,FILE_NAME_LEN);
		memset(userfile,0,FILE_NAME_LEN);
		memset(user,0,USER_NAME_LEN);
		memset(token,0,TOKEN_LEN);
		memset(fdfs_file_id,0,TEMP_BUF_MAX_LEN);
		memset(fdfs_file_url,0,FILE_URL_LEN);
		memset(boundary,0,TEMP_BUF_MAX_LEN);

	}		

	return 0;
}


