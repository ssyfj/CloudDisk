/*
*用于处理登录请求的CGI程序
*/

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "make_log.h"		//日志处理模块
#include "cfg.h"			//获取配置信息（关于redis和mysql）
#include "cJSON.h"			//JSON数据处理
#include "des.h"			//非对称加密算法
#include "md5.h"			//md5加密
#include "base64.h"			//base64
#include "deal_mysql.h"		//处理数据库模块
#include "redis_op.h"		//处理redis模块
#include "util_cgi.h"		//公共函数模块

//定义该文件模块和调用，用于日志的打印
#define LOGIN_LOG_MODULE "cgi"	
#define LOGIN_LOG_PROC "login"


//解析用户登录信息的json数据
int get_login_info(char *login_buf,char *username,char *pwd)
{
	int ret = 0;

	//解析json数据
	cJSON *root = cJSON_Parse(login_buf);
	if(root == NULL)
	{
		LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"cJSON_Parse err\n");
		ret = -1;
		goto failed;
	}

	cJSON *child1 = cJSON_GetObjectItem(root,"user");
	if(child1 == NULL)
	{
		LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"cJSON_GetObjectItem err\n");
		ret = -1;
		goto failed;
	}

	strcpy(username,child1->valuestring);

	cJSON *child2 = cJSON_GetObjectItem(root,"pwd");
	if(child2 == NULL)
	{
		LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"cJSON_GetObjectItem err\n");
		ret = -1;
		goto failed;
	}

	strcpy(pwd,child2->valuestring);

failed:
	if(root != NULL)
	{
		cJSON_Delete(root);
		root = NULL;
	}

	return ret;
}

//查询数据库，校验登录信息
int check_user_pwd(char *username,char *pwd)
{
	int ret = 0;
	char sql_cmd[SQL_MAX_LEN] = {0};

	//获取数据库配置信息
	char mysql_user[256] = {0};
	char mysql_pwd[256] = {0};
	char mysql_db[256] = {0};

	get_mysql_info(mysql_user,mysql_pwd,mysql_db);
    LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "mysql_user = %s, mysql_pwd = %s, mysql_db = %s\n", mysql_user, mysql_pwd, mysql_db);

	//开始连接数据库
	MYSQL *conn = mysql_conn(mysql_user,mysql_pwd,mysql_db);
	if(conn == NULL)
	{
		LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"mysql_conn error\n");
		return -1;;
	}

	//设置数据库编码
	mysql_query(conn,"set names utf8");

	//获取sql语句,这里没有做防止注入sql处理
	sprintf(sql_cmd,"select password from user_info where user_name='%s'",username);

	char tmp[256] = {0};

	process_result_one(conn,sql_cmd,tmp);	//获取一条结果
    LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "input password:%s , mysql password:%s\n", tmp, pwd);

	if(strcmp(tmp,pwd) == 0)				//登录成功
	{
		ret = 0;
	}
	else
	{
		ret = -1;							//登录失败
	}

	mysql_close(conn);						//关闭连接

	return ret;
}

//生成token字符串，保存在redis数据库中
int set_token(char *username,char *token)
{
	int ret = 0;
	redisContext *redis_conn = NULL;

	//redis服务器ip与端口
	char redis_ip[30] = {0};
	char redis_port[10] = {0};

	//读取redis配置信息
	get_redis_info(redis_ip,redis_port);
	LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"redis:[ip=%s,port=%s]\n",redis_ip,redis_port);

	//连接redis数据库
	redis_conn = rop_connectdb_nopwd(redis_ip,redis_port);
	if(redis_conn == NULL)
	{
		LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"redis connected error\n");
		ret = -1;
		goto failed;
	}

	//开始生成token
	int rand_num[4] = {0};
	int i = 0;

	srand((unsigned int)time(NULL));
	for(;i < 4; i++){
		rand_num[i] = rand()%1000;			//生成随机数
	}

	char tmp[1024] = {0};
	sprintf(tmp,"%s%d%d%d%d",username,rand_num[0],rand_num[1],rand_num[2],rand_num[3]);
	LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"tmp = %s\n",tmp);

	//进行加密
	char enc_tmp[1024*2] = {0};
	int enc_len = 0;
	//将des加密后的数据存放到enc_tmp中
	ret = DesEnc((unsigned char*)tmp,strlen(tmp),(unsigned char*)enc_tmp,&enc_len);	
	if(ret != 0)
	{
		LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"DesEnc error\n");
		ret = -1;
		goto failed;
	}

	//进行base64编码，会增大约33%
	char base64[1024*3] = {0};
	base64_encode((const unsigned char*)enc_tmp,enc_len,base64);
	LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"base64 = %s\n",base64);

	//进行md5加密
	MD5_CTX md5;
	MD5Init(&md5);
	unsigned char decrypt[16];				//md5加密后长度为128位，16字节
	MD5Update(&md5,(unsigned char*)base64,strlen(base64));	//对base64数据进行加密
	MD5Final(&md5,decrypt);

	char str[100] = {0};
	for(i = 0; i < 16;i++)
	{
		sprintf(str,"%02x",decrypt[i]);		//16进制输出
		strcat(token,str);
	}

	//将token保存到redis中去,有效时间24小时
	//setex key seconds value可以设置有效时间
	ret = rop_setex_string(redis_conn,username,86400,token);

failed:
	if(redis_conn != NULL)
	{
		rop_disconnect(redis_conn);
	}

	return ret;
}

/*
返回后台状态，json数据返回给前端
*/
void return_login_status(char *status_num,char *message)
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
	//阻塞等待用户连接
	while(FCGI_Accept() >= 0)
	{
		char *contentLength = getenv("CONTENT_LENGTH");
		int len;
		char token[TOKEN_LEN] = {0};

		printf("Content-type: text/html\r\n\r\n");

		if(contentLength == NULL)
		{
			len = 0;
		}
		else
		{
			len = atoi(contentLength);	
		}

		if(len <= 0)						//没有用户登录信息(出错会打印这个)
		{
			printf("No data from standrad input.<p>\n");
			LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"len = 0, No data from standrad input\n");
		}
		else								//去获取登录用户的信息
		{
			char buf[4*1024] = {0};
			int ret = 0;
			ret = fread(buf,1,len,stdin);	//从标准输入（web服务器）中读取数据
			if(ret == 0)
			{
				LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"fread(buf,1,len,stdin) error\n");
				continue;
			}

			LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"buf = %s\n",buf);

			//获取用户登录信息
			char username[USER_NAME_LEN] = {0};
			char pwd[PWN_LEN] = {0};
			get_login_info(buf,username,pwd);
			//调试开启，之后关闭
			LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"username = %s,pwd = %s\n",username,pwd);

			//登录判断
			ret = check_user_pwd(username,pwd);
			if(ret == 0)					//登录成功
			{
				//生成token字符串,存放一份到redis
				memset(token,0,sizeof(token));	
				ret = set_token(username,token);
				LOG(LOGIN_LOG_MODULE,LOGIN_LOG_PROC,"username = %s,token = %s\n",username,token);
			}

			if(ret == 0)					//返回登录成功信息
			{
				return_login_status("000",token);
			}
			else							//返回失败数据
			{
				return_login_status("001","fail");
			}
		}
	}

	return 0;
}