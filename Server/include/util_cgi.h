#ifndef __UTIL_CGI_H_
#define __UTIL_CGI_H_

#define FILE_NAME_LEN 		256		//文件名长度
#define MD5_LEN 	  		256		//MD5值，文件MD5
#define SUFFIX_LEN	  		8	    //后缀名长度

#define TIME_STRING_LEN     25    //时间戳长度

#define FILE_URL_LEN		512		//文件所存放storage的host_name

#define TEMP_BUF_MAX_LEN	512		//缓冲区长度

#define HOST_NAME_LEN 		30		//主机IP地址长度，storage所在服务器ip地址

#define USER_NAME_LEN   	128		//用户名长度
#define TOKEN_LEN 	  		128		//令牌长度
#define PWN_LEN				256		//密码长度

#define CHUNK_MAX_LEN 		1024	//每次读取数据的大小

#define UTIL_LOG_MODULE		"cgi"
#define UTIL_LOG_PROC		"util"

/*
处理字符串，截取两边的空白字符
成功返回0，失败返回-1
*/
int trim_space(char *buf);

/*
获取子串在主串中第一次出现的位置
full_data为主串，substr为子串
成功则返回匹配后的字符串首位置，失败返回NULL
*/
char* memstr(char *full_data,int full_data_len,char *substr);

/*
解析url query 类似abc=123&bbb=456字符串,传入一个key(abc),得到相应的value(123)
成功返回0，失败返回-1
*/
int query_parse_key_value(const char *query,const char *key,char *value,int *value_len);

/*
获取文件名file_name的后缀信息保存在suffix
成功返回0，失败返回-1
*/
int get_file_suffix(const char *file_name,char *suffix);

/*
实现对字符串strSrc的替换，将strFind替换为strReplace
其中strSrc是数组，需要判断数组空间size是否足够
成功返回0，失败返回-1
*/
int str_replace(char *strSrc, int size, const char *strFind, const char *strReplace);

/*
返回后台状态，json数据返回给前端
*/
void return_status(char *status_num,char *message);

/*
验证登录token，对大多数操作需要携带token，后台才进行处理
成功返回0，失败返回-1
*/
int verify_token(char *user,char *token);

#endif