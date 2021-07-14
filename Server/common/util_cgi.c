/**
 * @file util_cgi.c
 * @brief  cgi后台通用接口
 * @version 1.0
 * @date
 */
 
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "make_log.h"
#include "cJSON.h"
#include "cfg.h"
#include "redis_op.h"

#include "util_cgi.h"

/*
处理字符串，截取两边的空白字符
成功返回0，失败返回-1
*/
int trim_space(char *buf)
{
    int start = 0;
    int end = strlen(buf) - 1;
    int count;

    char *str = buf;

    if(str == NULL)
    {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "input buf == NULL\n");
        return -1;
    }

    while(isspace(str[start]) && str[start] != '\0')
    {
        start++;
    }

    while(isspace(str[end]) && end > start)
    {
        end--;
    }

    count = end - start + 1;

    strncpy(buf,str+start,count);
    buf[count] = '\0';

    return 0;
}

/*
获取子串在主串中第一次出现的位置
full_data为主串，substr为子串
成功则返回匹配后的字符串首位置，失败返回NULL
*/
char* memstr(char *full_data,int full_data_len,char *substr)
{
    if(full_data == NULL || full_data_len <= 0 || substr == NULL || *substr == '\0')
    {
        //LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "input params error!\n");
        return NULL;
    }

    int sublen = strlen(substr);
    int i = 0;
    char *cur = full_data;
    int last_pos = full_data_len - sublen + 1;

    //开始匹配
    for(;i<last_pos;i++)
    {
        if(*cur == *substr && memcmp(cur,substr,sublen) == 0)
        {
            //LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "get index!\n");
            return cur;
        }
        cur++;
    }

    return NULL;
}


/*
字符串按照字符进行分割，分割次数为cnt，分割结果保存在value中
成功返回0，失败返回-1,不足cnt返回1
*/
int strplit(char *full_data,char ch,VALUES value,int cnt)
{
    char *index_s = full_data;
    char *index_e = NULL;
    int count = 0;

    if(full_data == NULL || value == NULL || cnt <= 0)
    {
        return -1;
    }

    while(index_s != '\0'){
        index_e = strchr(index_s,ch);                       //开始查找索引
        if(index_e == NULL || count == cnt)                 //到达结尾
        {
            strncpy(value[count],index_s,full_data+strlen(full_data)-index_s);    //拷贝赋值
            break;
        }
        strncpy(value[count++],index_s,index_e-index_s);    //拷贝赋值
        index_s = index_e + 1;
    }

    return count == cnt ? 0 : 1;
}


/*
解析url query 类似abc=123&bbb=456字符串,传入一个key(abc),得到相应的value(123)
成功返回0，失败返回-1
*/
int query_parse_key_value(const char *query,const char *key,char *value,int *value_len)
{
    char *index_s = NULL;
    char *index_e = NULL;
    char *q = (char *)query;
    int len = 0;

    if(value == NULL || value_len == NULL)
    {
        return -1;
    }

    index_s = strstr(query,key);
    if(index_s == NULL || strchr(key,'=') != NULL)
    {
        return -1;
    }

    while((index_s = strstr(q,key)) != NULL)
    {
        if(index_s != query)
        {
            if(*(index_s-1) != '&') //判断前面一个字符是不是&
            {
                q = index_s + strlen(key);
                continue;
            }
        }
        //判断后一个字符是不是=
        index_s += strlen(key);
        if(*index_s != '=')
        {
            q = index_s;
            continue;
        }
        else
        {
            index_s ++; //寻value起始地址

            index_e = index_s;
            while(*index_e != '\0' && *index_e != '&' && *index_e != '#')
            {
                index_e++;
            }

            len = index_e - index_s;

            strncpy(value,index_s,len);
            value[len] = '\0';

            *value_len = len;
            return 0;
        }
    }


    return -1;
}

/*
获取文件名file_name的后缀信息保存在suffix
成功返回0，失败返回-1
*/
int get_file_suffix(const char *file_name,char *suffix)
{
    char *ptr_end = NULL;
    int len = strlen(file_name);
    int suf_len = 0;

    if(file_name == NULL || suffix == NULL)
    {
        return -1;
    }

    ptr_end = (char *)(file_name + len);
    while(*ptr_end != '.' && *ptr_end != *file_name)
    {
        ptr_end--;
    }

    if(*ptr_end == '.')
    {
        ptr_end++;
        suf_len = len - (ptr_end - file_name);
        if(suf_len != 0)
        {
            strncpy(suffix,ptr_end,suf_len);
            suffix[suf_len] = '\0';
        }
        else
        {
            strncpy(suffix,"null",5);
        }
    }
    else
    {
        strncpy(suffix,"null",5);
    }

    return 0;
}

/*
实现对字符串strSrc的替换，将strFind替换为strReplace
其中strSrc是数组，需要判断数组空间size是否足够
成功返回0，失败返回-1
*/
int str_replace(char *strSrc, int size, const char *strFind, const char *strReplace)
{
    int ret = 0;
    char *temp = calloc(size,sizeof(char));
    char *src = strSrc;
    int len_f = strlen(strFind);
    int len_r = strlen(strReplace);

    if(strSrc == NULL || strFind == NULL || strReplace == NULL)
    {
        return -1;
    }

    while(*src != '\0')
    {
        char *index = strstr(src,strFind);
        if(index == NULL)   //匹配完成
        {
            //判断空间
            if(strlen(temp)+strlen(src) > size)
            {
                ret = -1;
            }
            else
            {
                strncpy(temp+strlen(temp),src,strlen(src));
            }
            break;
        }
        else                //还需要继续匹配
        {
            if(strlen(temp)+index-src+len_r > size)
            {
                ret = -1;
                break;
            }
            strncpy(temp+strlen(temp),src,index - src);
            strncpy(temp+strlen(temp),strReplace,len_r);

            src += index - src + len_f;
        }
    }

    if(ret == 0)
    {
        strncpy(strSrc,temp,strlen(temp));
    }
    free(temp);
    return ret;
}

/*
返回后台状态，json数据返回给前端
*/
void return_status(char *status_num,char *message)
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

/*
验证登录token，对大多数操作需要携带token，后台才进行处理
成功返回0，失败返回-1
*/
int verify_token(char *user,char *token)
{
    int ret = 0;

    redisContext *redis_conn = NULL;
    char tmp_token[TOKEN_LEN] = {0};

    //redis服务器ip与端口
    char redis_ip[30] = {0};
    char redis_port[10] = {0};

    //读取redis配置信息
    get_redis_info(redis_ip,redis_port);
    LOG(UTIL_LOG_MODULE,UTIL_LOG_PROC,"redis:[ip=%s,port=%s]\n",redis_ip,redis_port);

    //连接redis数据库
    redis_conn = rop_connectdb_nopwd(redis_ip,redis_port);
    if(redis_conn == NULL)
    {
        LOG(UTIL_LOG_MODULE,UTIL_LOG_PROC,"redis connected error\n");
        ret = -1;
        goto failed;
    }

    //获取对应的token
    ret = rop_get_string(redis_conn,user,tmp_token);
    if(ret == 0)
    {
        if(strcmp(token,tmp_token) != 0)
        {
            ret = -1;
        }
    }

failed:
    if(redis_conn != NULL)
    {
        rop_disconnect(redis_conn);
    }

    return ret;
}