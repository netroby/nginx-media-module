#ifndef _NGX_MEDIA_SYS_STAT_H__
#define _NGX_MEDIA_SYS_STAT_H__
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif

/********************* function begin *********************/
ngx_int_t ngx_media_sys_stat_init();

void      ngx_media_sys_stat_release();

ngx_int_t ngx_media_sys_stat_add_networdcard(u_char* strIP);

ngx_int_t ngx_media_sys_stat_remove_networdcard(u_char* strIP);

ngx_int_t ngx_media_sys_stat_add_disk(u_char* strDiskPath);

ngx_int_t ngx_media_sys_stat_remove_disk(u_char* strDiskPath);

void      ngx_media_sys_stat_get_cpuinfo(ngx_uint_t *ulUsedPer);

void      ngx_media_sys_stat_get_memoryinfo(ngx_uint_t* ulTotalSize, ngx_uint_t* ulUsedSize);

ngx_int_t ngx_media_sys_stat_get_networkcardinfo(u_char* strIP, ngx_uint_t* ulTotalSize,
                   ngx_uint_t* ulUsedRecvSize, ngx_uint_t* ulUsedSendSize);

ngx_int_t ngx_media_sys_stat_get_diskinfo(u_char* strDiskPath, ngx_uint_t* ullTotalSize, ngx_uint_t* ullUsedSize);

#endif


