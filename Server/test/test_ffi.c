/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.fastken.com/ for more detail.
**/


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
//fastfds头文件
#include "fastdfs/tracker_types.h"				//一些定义存放在这里
#include "fastdfs/fdfs_client.h"
#include "fastcommon/logger.h"

//自定义头文件
#include "deal_mysql.h"							//处理数据库模块
#include "cfg.h"								//配置信息
#include "make_log.h"							//日志处理模块	
#include "redis_op.h"							//处理redis模块
#include "util_cgi.h" 							//cgi后台通用接口


#define UPLOAD_LOG_MODULE "cgi"
#define UPLOAD_LOG_PROC   "upload"


void printf_file_info(char *fdfs_file_id,char *confinfo)
{
	printf("\n===============\n");
	const char *file_type_str;
    int result;
    FDFSFileInfo file_info;

    log_init();
    g_log_context.log_level = LOG_ERR;
    ignore_signal_pipe();

    if ((result=fdfs_client_init(confinfo)) != 0)
    {
            return;
    }

    memset(&file_info, 0, sizeof(file_info));
    result = fdfs_get_file_info_ex1(fdfs_file_id, true, &file_info);
    if (result != 0)
    {
            fprintf(stderr, "query file info fail, " \
                    "error no: %d, error info: %s\n", \
                    result, STRERROR(result));
    }
    else
    {
        char szDatetime[32];

        switch (file_info.file_type)
        {
            case FDFS_FILE_TYPE_NORMAL:
                file_type_str = "normal";
                break;
            case FDFS_FILE_TYPE_SLAVE:
                file_type_str = "slave";
                break;
            case FDFS_FILE_TYPE_APPENDER:
                file_type_str = "appender";
                break;
            default:
                file_type_str = "unkown";
                break;
        }

        printf("GET FROM SERVER: %s\n\n",
        file_info.get_from_server ? "true" : "false");
        printf("file type: %s\n", file_type_str);
        printf("source storage id: %d\n", file_info.source_id);
        printf("source ip address: %s\n", file_info.source_ip_addr);
        printf("file create timestamp: %s\n", formatDatetime(
                file_info.create_timestamp, "%Y-%m-%d %H:%M:%S", \
                szDatetime, sizeof(szDatetime)));
        printf("file size: %"PRId64"\n", \
                file_info.file_size);
        printf("file crc32: %d (0x%08x)\n", \
                file_info.crc32, file_info.crc32);
    }


    tracker_close_all_connections();
    fdfs_client_destroy();
}


int main(int argc, char *argv[])
{
	printf_file_info(argv[1],argv[2]);	
	return 0;
}

