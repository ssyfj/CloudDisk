/*
*用于处理文件上传的CGI程序
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
//文件操作、进程操作头文件
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>								//包含进程的处理
#include <sys/wait.h>
//fastcgi头文件
#include "fcgi_config.h"


int main()
{
	printf("999");
	return 0;
}
