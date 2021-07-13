#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

int trim_space(char *buf)
{
    int start = 0;
    int end = strlen(buf) - 1;
    int count;

    char *str = buf;

    if(str == NULL)
    {
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

/*为主串，substr为子串
成功则返回匹配后的字符串首位置，失败返回NULL
*/
char* memstr(char *full_data,int full_data_len,char *substr)
{
    if(full_data == NULL || full_data_len <= 0 || substr == NULL)
    {
        return NULL;
    }

    if(*substr == '\0')
    {
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
            return cur;
        }
        cur++;
    }

    return NULL;
}

/*
解析url query 类似abc=123&bbb=456字符串,传入一个key(abc),得到相应的value(123)
成功返回0，失败返回-1
*/
int query_parse_key_value(const char *query,const char *key,char *value,int *value_len)
{
    char *index_s = NULL;
    char *index_e = NULL;
    char *q = query;
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


void testSpace()
{
    char buf[100] = " fsag gaw gaw        ";
    //测试空格截取
    trim_space(buf);
    printf("%s\n",buf);
}

void testMemstr()
{
    char src[100] = " sa fwag hea@fag5341gehh";
    char sub[100] = "fag";
    char *idx = memstr(src,strlen(src),sub);
    printf("%s\n",idx);
}

void testQuery()
{
   char url[100] = "aaa=123&&bbb=456&&bb=68";
   char key[100] = "bb";
   char val[100] = {0};
   int len = 0;
   query_parse_key_value(url,key,val,&len);
   printf("%s-%d\n",val,len);
}

void testSuffix()
{
    char filename[100] = "dsa.fwa.gwag.gwa.png";
    char suffix[100] = {0};
    get_file_suffix(filename,suffix);
    printf("%s\n",suffix);  
}

void testReplace()
{
    char src[30] = "agwg gehhr 53qtg 3q44faqtgga";
    char find[10] = "qtg";
    char rep[20] = "gwankkh8";
    str_replace(src,30,find,rep);
    printf("%s\n",src);
}

int main()
{
    //testSpace();
    //testMemstr();
    //testQuery();
    //testSuffix();
    testReplace();
    return 0;
}
