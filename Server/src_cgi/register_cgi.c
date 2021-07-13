/*
*用于处理注册请求的CGI程序
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
#include "deal_mysql.h"		//处理数据库模块
#include "util_cgi.h"		//公共函数模块

//定义该文件模块和调用，用于日志的打印
#define REGISTER_LOG_MODULE "cgi"	
#define REGISTER_LOG_PROC "register"

int getRegisterInfo(char *reg_buf,char *username, char *nickname, char *password, char *phone,char *email)
{
	int ret = 0;

	//解析json数据
	cJSON *root = cJSON_Parse(reg_buf);
	if(root == NULL)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"cJSON_Parse err\n");
		ret = -1;
		goto failed;
	}

	cJSON *child1 = cJSON_GetObjectItem(root,"username");
	if(child1 == NULL)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"cJSON_GetObjectItem err\n");
		ret = -1;
		goto failed;
	}

	strcpy(username,child1->valuestring);

	cJSON *child2 = cJSON_GetObjectItem(root,"nickname");
	if(child2 == NULL)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"cJSON_GetObjectItem err\n");
		ret = -1;
		goto failed;
	}

	strcpy(nickname,child2->valuestring);

	cJSON *child3 = cJSON_GetObjectItem(root,"password");
	if(child3 == NULL)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"cJSON_GetObjectItem err\n");
		ret = -1;
		goto failed;
	}

	strcpy(password,child3->valuestring);


	cJSON *child4 = cJSON_GetObjectItem(root,"phone");
	if(child4 == NULL)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"cJSON_GetObjectItem err\n");
		ret = -1;
		goto failed;
	}

	strcpy(phone,child4->valuestring);


	cJSON *child5 = cJSON_GetObjectItem(root,"email");
	if(child3 == NULL)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"cJSON_GetObjectItem err\n");
		ret = -1;
		goto failed;
	}

	strcpy(email,child5->valuestring);

failed:
	if(root != NULL)
	{
		cJSON_Delete(root);
		root = NULL;
	}

	return ret;
}

int register_user_info(char *username,char *nickname,char *password,char *phone,char *email)
{
	int ret = 0;
	char sql_cmd[SQL_MAX_LEN] = {0};

	//获取数据库配置信息
	char mysql_user[256] = {0};
	char mysql_pwd[256] = {0};
	char mysql_db[256] = {0};

	get_mysql_info(mysql_user,mysql_pwd,mysql_db);
    LOG(REGISTER_LOG_MODULE, REGISTER_LOG_PROC, "mysql_user = %s, mysql_pwd = %s, mysql_db = %s\n", mysql_user, mysql_pwd, mysql_db);

	//开始连接数据库
	MYSQL *conn = mysql_conn(mysql_user,mysql_pwd,mysql_db);
	if(conn == NULL)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"mysql_conn error\n");
		return -1;;
	}

	//设置数据库编码
	mysql_query(conn,"set names utf8");

	sprintf(sql_cmd,"select id from user_info where user_name='%s' or nick_name='%s'",username,nickname);

	ret = process_result_exists(conn,sql_cmd);
	if(ret != 0)						
	{
		ret =  -2;								//用户名或者昵称重复
		goto failed;
	}

	//获取sql语句,这里没有做防止注入sql处理
	sprintf(sql_cmd,"insert into user_info(user_name,nick_name,password,phone,email) values('%s','%s','%s','%s','%s')",
		username,nickname,password,phone,email);

	if(mysql_query(conn,sql_cmd) != 0)
	{
		LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"mysql_query insert err[%d]:%s\n",mysql_errno(conn),mysql_error(conn));
		ret = -1;								//插入失败
		goto failed;
	}

    LOG(REGISTER_LOG_MODULE, REGISTER_LOG_PROC, "mysql_query insert success: user[%s]\n",username);

failed:
	if(conn != NULL)
	{
		mysql_close(conn);						//关闭连接
	}

	return ret;
}

//返回结果状态，json
void return_register_status(char *status_num,char *status_msg)
{
	char *out = NULL;
	cJSON *root = cJSON_CreateObject();		//创建json对象
	cJSON_AddStringToObject(root,"code",status_num);
	cJSON_AddStringToObject(root,"message",status_msg);
	out = cJSON_Print(root);				//json转字符串

	cJSON_Delete(root);
	
	if(out != NULL)
	{
		printf(out);						//反馈数据给前端
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
		char token[128] = {0};

		printf("Content-type: text/html\r\n\r\n");

		if(contentLength == NULL)
		{
			len = 0;
		}
		else
		{
			len = atoi(contentLength);	
		}

		if(len <= 0)						//没有用户的注册信息(出错会打印这个)
		{
			printf("No data from standrad input.<p>\n");
			LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"len = 0, No data from standrad input\n");
		}
		else								//去获取注册用户的信息
		{
			char buf[4*1024] = {0};
			int ret = 0;
			ret = fread(buf,1,len,stdin);	//从标准输入（web服务器）中读取数据
			if(ret == 0)
			{
				LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"fread(buf,1,len,stdin) error\n");
				continue;
			}

			LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"buf = %s\n",buf);

			//获取用户登录信息
			char username[512] = {0};
			char nickname[512] = {0};
			char password[512] = {0};
			char phone[512] = {0};
			char email[512] = {0};

			getRegisterInfo(buf,username,nickname,password,phone,email);
			//调试开启，之后关闭
			LOG(REGISTER_LOG_MODULE,REGISTER_LOG_PROC,"Register:username = %s,nickname = %s,password = %s,phone = %s,email = %s\n",
				username,nickname,password,phone,email);

			//登录判断
			ret = register_user_info(username,nickname,password,phone,email);

			if(ret == 0)					//返回登录成功信息
			{
				return_register_status("002","success");
			}
			else if(ret == -2)				//用户名已经存在
			{
				return_register_status("003","username or nickname exists, failed to register!");
			}
			else							//返回失败数据
			{
				return_register_status("004","fail");
			}
		}
	}

	return 0;
}