/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/


#ifndef _NGX_MEDIA_LICENSE_H_INCLUDED_
#define _NGX_MEDIA_LICENSE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif

ngx_uint_t ngx_media_license_task_count();
ngx_uint_t ngx_media_license_rtmp_channle();
void       ngx_media_license_rtmp_count(ngx_uint_t count);
ngx_uint_t ngx_media_license_rtmp_current();
ngx_uint_t ngx_media_license_rtsp_channle();
ngx_uint_t ngx_media_license_rtsp_current();
ngx_uint_t ngx_media_license_hls_channle();
void       ngx_media_license_hls_count(ngx_uint_t count);
ngx_uint_t ngx_media_license_hls_current();




#endif /* _NGX_MEDIA_LICENSE_H_INCLUDED_ */

