#ifndef __MAKE_LOG_H_
#define __MAKE_LOG_H_
#include <pthread.h>	//引入线程头文件，是为了使用锁，防止写入日志文件出错

//输出日志buf到对应的path日志文件中去
int out_put_file(char *path, char *buf);
//创建目录，防止写入日志时，目录不存在出错
int make_path(char *path,char *module_name,char *proc_name);
//对外调用函数，打印日志
int dumpmsg_to_file(char *module_name,char *proc_name,
						const char *filename,int line,
						const char *funcname,char *fmt, ...);

//关于宏定义的参数省略：https://blog.csdn.net/bytxl/article/details/46008529, ...为参数省略，##为参数展开
#define LOG(module_name,proc_name,args...) \
		do{	\
		dumpmsg_to_file(module_name,proc_name,__FILE__,__LINE__,__FUNCTION__, ##args); \
	}while(0)	//使用代码段，可以避免宏定义带来的替换问题

extern pthread_mutex_t ca_log_lock;	//定义全局锁

#endif