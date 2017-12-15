/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/


#ifndef _NGX_MEDIA_LIVE_SESSION_H_INCLUDED_
#define _NGX_MEDIA_LIVE_SESSION_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif
ngx_int_t ngx_media_live_session_exist(ngx_str_t* name);
ngx_int_t ngx_media_live_session_create(ngx_str_t* name,ngx_str_t* play,ngx_str_t*play_done,ngx_uint_t ttl);
ngx_int_t ngx_media_live_session_update(ngx_str_t* name,ngx_uint_t flux);
#endif /* _NGX_MEDIA_LIVE_SESSION_H_INCLUDED_ */


