/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_live_hls_module.c
 Version    : V 1.0.0
 Date       : 2016-04-28
 Author     : hexin H.kernel
 Modify     :
            1.2016-04-28: create
            2.2016-04-29: add the video task task
******************************************************************************/


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_cycle.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>
#include <ngx_thread.h>
#include <ngx_thread_pool.h>
#include "ngx_media_live_module.h"
#include "ngx_media_license_module.h"
#include "ngx_media_include.h"
#include "ngx_media_live_cache_module.h"






typedef struct {
    ngx_str_t                       on_play;
    ngx_str_t                       on_play_done;
    ngx_str_t                       session;
    ngx_str_t                       playlist;
    ngx_str_t                       mediadata;
    void*                           playlist_cache;
    void*                           mediadata_cache;
} ngx_media_live_loc_conf_t;




static char*     ngx_media_live_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_media_live_init_process(ngx_cycle_t *cycle);
static void      ngx_media_live_exit_process(ngx_cycle_t *cycle);
static void*     ngx_media_live_create_loc_conf(ngx_conf_t *cf);


static ngx_command_t  ngx_media_live_commands[] = {

    { ngx_string(NGX_MEDIA_LIVE),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS|NGX_CONF_TAKE1,
      ngx_media_live_init,
      0,
      0,
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_ONPLAY),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, on_play),
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_ONPLAY_DONE),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, on_play_done),
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_PLAY_TIMEOUT),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t,timeout),
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_CACHE_SESSION),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, session),
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_CACHE_PLAYLIST),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, playlist),
      NULL },

    { ngx_string(NGX_MEDIA_LIVE_CACHE_MEDIADATA),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_media_live_loc_conf_t, mediadata),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_media_live_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_media_live_create_loc_conf,         /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_live_module = {
    NGX_MODULE_V1,
    &ngx_media_live_module_ctx,            /* module context */
    ngx_media_live_commands,               /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_media_live_init_process,           /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_media_live_exit_process,           /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_media_live_hls_handler(ngx_http_request_t *r)
{
    ngx_int_t    rc;
    ngx_media_live_loc_conf_t* conf;


    conf = ngx_http_get_module_loc_conf(r, ngx_media_live_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live module conf fail.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    ngx_uint_t count   = ngx_media_live_hls_get_cur_count();
    ngx_uint_t licesen = ngx_media_license_hls_channle();

    if (count >= licesen)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                      "hls session is full,count:[%d],license:[%d].",count,licesen);
        return NGX_HTTP_NOT_ALLOWED;
    }

    /* discard request body, since we don't need it here */
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    /* return the NGX_DONE and send response by the ngx_media_live_hls_recv_request */
    return NGX_DONE;
}
static ngx_int_t
ngx_media_live_dash_handler(ngx_http_request_t *r)
{
    /* TODO: to support the dash */
    return NGX_HTTP_NOT_ALLOWED;
}


static void *
ngx_media_live_create_loc_conf(ngx_conf_t *cf)
{
    ngx_media_live_loc_conf_t  *conf;
    ngx_pool_t                 *pool = NULL;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_live_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }


    ngx_str_null(&conf->on_play);
    ngx_str_null(&conf->on_play_done);
    ngx_conf_init_msec_value(conf->timeout, NGX_CONF_UNSET_MSEC);
    ngx_str_null(&conf->session);
    ngx_str_null(&conf->playlist);
    ngx_str_null(&conf->mediadata);
    conf->playlist_cache  = NULL;
    conf->mediadata_cache = NULL;



    return conf;
}

static char*
ngx_media_live_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    ngx_str_t                 *value;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_live_hls_handler;/* default is hls handler */

    if(1 >= cf->args->nelts) {
        return NGX_CONF_OK;
    }
    value = cf->args->elts;
    if (ngx_strcasecmp(value[1].data, (u_char *) "hls") == 0)
	{
		clcf->handler = ngx_media_live_hls_handler
	}
	else if (ngx_strcasecmp(value[1].data, (u_char *) "dash") == 0)
	{
		clcf->handler = ngx_media_live_dash_handler;
	}
	else
	{
		clcf->handler = ngx_media_live_hls_handler;/* default is hls handler */
	}

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_media_live_init_process(ngx_cycle_t *cycle)
{
    if(NULL == g_session_ctx) {
        return NGX_OK;
    }
    return NGX_OK;
}

static void
ngx_media_live_exit_process(ngx_cycle_t *cycle)
{
    /* destory the session manage */
    return ;
}

ngx_uint_t ngx_media_live_hls_get_cur_count()
{
}




