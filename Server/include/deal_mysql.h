#ifndef __DEAL_MYSQL_H_
#define __DEAL_MYSQL_H_

#include <mysql/mysql.h>

#define SQL_MAX_LEN 512

/*
用于打印（操作）数据库出错时的错误信息

@param conn		连接数据库的句柄，可以获取数据库传递出来的错误信息
@param title 	用户错误提示信息,用户传入
*/
void print_error(MYSQL *conn,const char *title);

//连接数据库，获取数据库的句柄
MYSQL* mysql_conn(char *user_name,char *passwd, char *db_name);

//处理数据库查询结果，结果集保存到buf
//这里只去获取一条数据，多余的数据不需要
int process_result_one(MYSQL *conn,char *sql_cmd,char *buf);

//用来查询数据是否存在
int process_result_exists(MYSQL *conn,char *sql_cmd);

#endif