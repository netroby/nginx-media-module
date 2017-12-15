/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/


#ifndef _NGX_MEDIA_LIVE_H_INCLUDED_
#define _NGX_MEDIA_LIVE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>


#if (NGX_WIN32)
typedef __int8              int8_t;
typedef unsigned __int8     uint8_t;
#endif


ngx_uint_t ngx_media_live_hls_get_cur_count();




#endif /* _NGX_MEDIA_LIVE_H_INCLUDED_ */


