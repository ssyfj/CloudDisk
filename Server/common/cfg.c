#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"                  //JSON数据处理
#include "make_log.h"               //日志处理
#include "cfg.h"        



//定义函数从配置文件中读取数据参数
int get_cfg_value(const char *profile,char *title,char *key,char *value)
{
    int ret = 0;

    char *buf = NULL;
    FILE *fp = NULL;

    if(profile == NULL || title == NULL || key == NULL || value == NULL){
        return -1;
    }

    fp = fopen(profile,"rb");       //只读方式打开文件
    if(fp == NULL){                 //文件打开失败
        fprintf(stderr, "fopen error\n");
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"fopen error\n");
        ret = -1;
        goto failed;
    }

    fseek(fp,0,SEEK_END);           //光标移动到文件末尾
    long size = ftell(fp);          //获取文件数据大小
    fseek(fp,0,SEEK_SET);           //重新移动光标到开头

    buf = (char *)calloc(1,size+1); //动态分配空间，calloc优点就是会初始化空间数据
    if(buf == NULL){
        fprintf(stderr, "calloc error\n");
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"calloc error\n");
        ret = -1;
        goto failed;
    }

    fread(buf,1,size,fp);           //开始读取文件数据,全部数据

    cJSON *root = cJSON_Parse(buf); //开始解析json数据
    if(root == NULL){
        fprintf(stderr, "cJSON_Parse error\n");
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"cJSON_Parse error\n");
        ret = -1;
        goto failed;
    }

    //开始返回需要的数据
    cJSON *parent = cJSON_GetObjectItem(root,title);
    if(parent == NULL){
        fprintf(stderr, "title error\n");
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"title error\n");
        ret = -1;
        goto failed;
    }

    cJSON *child = cJSON_GetObjectItem(parent,key);
    if(child == NULL){
        fprintf(stderr, "key error\n");
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"key error\n");
        ret = -1;
        goto failed;
    }

    strcpy(value,child->valuestring); //获取value，返回给调用函数

    cJSON_Delete(root);             //删除json对象
failed:
    if(fp != NULL){
        fclose(fp);
    }  

    if(buf != NULL){
        free(buf);
    }

    return ret;
}

//获取数据库信息，用户名、密码、所用数据库
extern int get_mysql_info(char *mysql_user,char *mysql_pwd,char *mysql_db)
{
    if(get_cfg_value(CFG_PATH,"mysql","user",mysql_user) == -1){
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"get mysql user error!\n");
        return -1;
    }

    if(get_cfg_value(CFG_PATH,"mysql","password",mysql_pwd) == -1){
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"get mysql password error!\n");
        return -1;
    }

    if(get_cfg_value(CFG_PATH,"mysql","database",mysql_db) == -1){
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"get mysql database error!\n");
        return -1;
    }

    return 0;
}

//获取redis信息，ip和端口
extern int get_redis_info(char *redis_ip,char *redis_port)
{
    if(get_cfg_value(CFG_PATH,"redis","ip",redis_ip) == -1){
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"get mysql database error!\n");
        return -1;
    }

    if(get_cfg_value(CFG_PATH,"redis","port",redis_port) == -1){
        LOG(CFG_LOG_MODULE,CFG_LOG_PROC,"get mysql database error!\n");
        return -1;
    }

    return 0;        
}