#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#include "make_log.h"
#include <pthread.h>

pthread_mutex_t ca_log_lock = PTHREAD_MUTEX_INITIALIZER;    //定义全局锁

//总写入方法，调用目录创建和日志写入方法
//使用案例：LOG(LOGIN_LOG_MODULE, LOGIN_LOG_PROC, "mysql_user = %s, mysql_pwd = %s, mysql_db = %s\n", mysql_user, mysql_pwd, mysql_db);
//LOGIN_LOG_MODULE, LOGIN_LOG_PROC分别是cgi与login
int dumpmsg_to_file(char *module_name,char *proc_name,
                        const char *filename,int line,
                        const char *funcname,char *fmt, ...)
{
    int ret = 0;
    char msg[4096] = {0};
    char buf[4096] = {0};
    char filepath[1024] = {0};
    time_t t = 0;
    struct tm *now=NULL;

    va_list al;                     //解决变参问题的一组宏

    time(&t);                       //获取时间戳
    now = localtime(&t);            //将时间戳转换为yyyy-mm-dd hh:mm:ss格式

    //开始处理可变参数，就行格式化处理（将...参数输入到fmt)
    va_start(al,fmt);
    vsprintf(msg,fmt,al);
    va_end(al);

    //合并数据，形成一条日志
    snprintf(buf,4096,"[%04d-%02d-%02d %02d:%02d:%02d]--[%s:%d]--%s", 
        now->tm_year+1900,now->tm_mon+1,now->tm_mday,now->tm_hour,now->tm_min,now->tm_sec,  //打印日期
        filename,line,msg);         //文件名、行号、输入的日志信息

    //开始创建目录,通过数组filepath返回路径
    make_path(filepath,module_name,proc_name);

    //开始打印日志到文件中
    pthread_mutex_lock(&ca_log_lock);
    ret = out_put_file(filepath,buf);
    pthread_mutex_unlock(&ca_log_lock);

    return ret;
}

//输出日志buf到对应的path日志文件中去
int out_put_file(char *path, char *buf)
{
    int fd = open(path,O_RDWR | O_CREAT | O_APPEND,0777);

    if(fd == -1){
        return -1;
    }

    if(write(fd,buf,strlen(buf)) != (int)strlen(buf)){
        fprintf(stderr, "write error\n");
        close(fd);
        return -2;
    }else{
        close(fd);
    }
    return 0;
}

//创建目录，防止写入日志时，目录不存在出错
//该方法出错的地方太多，交给上层函数dumpmsg_to_file处理错误，错误出现在out_put_file中，比如目录创建失部，写入日志必然报错
int make_path(char *path,char *module_name,char *proc_name)
{
    //获取时间信息，目录创建与时间有关
    time_t t;   
    struct tm *now = NULL;

    //为各级目录创建空间，方便后面就行判断
    char top_dir[1024] = {"."};
    char second_dir[1024] = {"./logs"};
    char third_dir[1024] = {0};     //与模块有关

    char y_dir[1024] = {0};         //与日期相关
    char m_dir[1024] = {0};         

    //获取当前时间
    time(&t);
    now = localtime(&t);

    //获取文件路径，返回给dumpmsg_to_file方法去打印日志到路径文件下
    snprintf(path,1024,"./logs/%s/%04d/%02d/%s-%02d.log",module_name,
        now->tm_year+1900,now->tm_mon+1,proc_name,now->tm_mday);

    //下面开始检查路径下的目录，进行创建
    sprintf(third_dir,"%s/%s",second_dir,module_name);
    sprintf(y_dir,"%s/%04d",third_dir,now->tm_year+1900);
    sprintf(m_dir,"%s/%02d",y_dir,now->tm_mon+1);

    //access(,0)判断文件/目录是否存在，存在返回0，不存在返回-1
    if(access(top_dir,0) == -1){
        if(mkdir(top_dir,0777) == -1){
            fprintf(stderr, "create %s failed!\n", top_dir);
        }else if(mkdir(second_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", top_dir,second_dir);
        }else if(mkdir(third_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", top_dir,third_dir);
        }else if(mkdir(y_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", top_dir,y_dir);
        }else if(mkdir(m_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", top_dir,m_dir);
        }
    }else if(access(second_dir,0) == -1){
        if(mkdir(second_dir,0777) == -1){
            fprintf(stderr, "create %s failed!\n", second_dir);
        }else if(mkdir(third_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", second_dir,third_dir);
        }else if(mkdir(y_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", second_dir,y_dir);
        }else if(mkdir(m_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", second_dir,m_dir);
        }        
    }else if(access(third_dir,0) == -1){
        if(mkdir(third_dir,0777) == -1){
            fprintf(stderr, "create %s failed!\n", third_dir);
        }else if(mkdir(y_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", third_dir,y_dir);
        }else if(mkdir(m_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", third_dir,m_dir);
        }                
    }else if(access(y_dir,0) == -1){
        if(mkdir(y_dir,0777) == -1){
            fprintf(stderr, "create %s failed!\n", y_dir);
        }else if(mkdir(m_dir,0777) == -1){
            fprintf(stderr, "%s:create %s failed!\n", y_dir,m_dir);
        }          
    }else if(access(m_dir,0) == -1){
        if(mkdir(m_dir,0777) == -1){
            fprintf(stderr, "create %s failed!\n", m_dir);
        }                  
    }
    return 0;
}

