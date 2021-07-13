#ifndef __CFG_H_
#define __CFG_H_

//定义配置文件路径
#define CFG_PATH "./conf/cfg.json"

//定义日志打印的模块和调用
#define CFG_LOG_MODULE "cgi"
#define CFG_LOG_PROC "cfg"

#define FDFS_CLI_CONF_PATH_LEN 256					//fdfs系统中client的配置文件路径

//定义函数从配置文件中读取数据参数
extern int get_cfg_value(const char *profile,char *title,char *key,char *value);

//获取数据库信息，用户名、密码、所用数据库
extern int get_mysql_info(char *mysql_user,char *mysql_pwd,char *mysql_db);

//获取redis信息，ip和端口
extern int get_redis_info(char *redis_ip,char *redis_port);

#endif