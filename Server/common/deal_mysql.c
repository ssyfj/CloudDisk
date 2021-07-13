#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deal_mysql.h"

//用于打印（操作）数据库出错时的错误信息
void print_error(MYSQL *conn,const char *title)
{
    fprintf(stderr, "%s\nError %u (%s)\n", title,mysql_errno(conn),mysql_error(conn));
}

//连接数据库
MYSQL* mysql_conn(char *user_name,char *passwd,char *db_name)
{
    MYSQL *conn = NULL;             //MYSQL对象句柄

    conn = mysql_init(NULL);        //初始化一个MYSQL句柄，用来连接mysql服务端
    if(conn == NULL){
        fprintf(stderr, "mysql initial failed\n");
        return NULL;
    }

    //mysql_real_connect()尝试与运行在主机上的MySQL数据库引擎建立连接
    //conn: 是已有MYSQL结构的地址。调用mysql_real_connect()之前，必须调用mysql_init()来初始化MYSQL结构。
    //NULL: 值必须是主机名或IP地址。如果值是NULL或字符串"localhost"，连接将被视为与本地主机的连接。
    //user_name: 用户的MySQL登录ID
    //passwd: 参数包含用户的密码
    if(mysql_real_connect(conn,NULL,user_name,passwd,db_name,0,NULL,0) == NULL)
    {
        fprintf(stderr, "mysql connect failed: Error %u(%s)\n",mysql_errno(conn), mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }    

    return conn;
}

//-------------------------------mysql查询-------------------------------
//查询数据是否存在
int process_result_exists(MYSQL *conn,char *sql_cmd)
{
    int ret = 1;
    MYSQL_RES *res_set = NULL;          //结果集指针

    if(mysql_query(conn,sql_cmd) != 0)  //进行数据查询
    {
        print_error(conn,"mysql_query error!\n");
        ret = -1;
        goto failed;
    }

    res_set = mysql_store_result(conn); //生成结果集
    if(res_set == NULL)
    {
        print_error(conn,"mysql_store_result error!\n");
        ret = -1;
        goto failed;
    }

    MYSQL_ROW row;
    ulong line = 0;

    //mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    line = mysql_num_rows(res_set);
    if(line == 0)
    {
        ret = 0;                        //没有查询到数据
        goto failed;
    }

failed:
    if(res_set != NULL)                 //表示还有数据，但是我们不需要了
    {
        mysql_free_result(res_set);
    }
    return ret;
}

//处理数据库查询结果，结果集保存到buf
//这里只去获取一条数据，多余的数据不需要
int process_result_one(MYSQL *conn,char *sql_cmd,char *buf)
{
    int ret = 0;
    MYSQL_RES *res_set = NULL;          //结果集指针

    if(buf == NULL)                     //数据没有办法返回给buf
    {
        ret = 2;
        goto failed;
    }

    if(mysql_query(conn,sql_cmd) != 0)  //进行数据查询
    {
        print_error(conn,"mysql_query error!\n");
        ret = -1;
        goto failed;
    }

    res_set = mysql_store_result(conn); //生成结果集
    if(res_set == NULL)
    {
        print_error(conn,"mysql_store_result error!\n");
        ret = -1;
        goto failed;
    }

    MYSQL_ROW row;
    ulong line = 0;

    //mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    line = mysql_num_rows(res_set);
    if(line == 0)
    {
        ret = 1;                        //没有查询到数据
        goto failed;
    }

    //可能存在多个数据，我们只需要去获取一行数据
    //如果需要获取所有数据，一般使用while循环
    if((row = mysql_fetch_row(res_set)) != NULL)
    {
        if(row[0] != NULL)
        {
            strcpy(buf,row[0]);         //保存数据
        }
    }

failed:
    if(res_set != NULL)                 //表示还有数据，但是我们不需要了
    {
        mysql_free_result(res_set);
    }
    return ret;
}

