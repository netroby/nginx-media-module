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
#include "ngx_buffer_cache.h"


static ngx_str_t    shm_name = ngx_string("media_live_limit");


/************************ hls session control *******************************************
 *  1. request:/streamname.m3u8--->create session
 *             ---> send on_play ---->rewrite /streamname.m3u8?token=sessionID
 *  2. request:/streamname.m3u8?token=sessionID ---> reconstruct m3u8 playlist
 *             ---> update session ---->/xxx.ts?token=sessionID
 *  3. request:/xxx.ts?token=sessionID ---> update session --->response with static file
 *  4. timer check session: not active session release and send on_play_done
 *
 ***************************************************************************************/
#define MEDIA_LIVE_M3U8      ".m3u8"
#define MEDIA_LIVE_TS        ".ts"
#define MEDIA_LIVE_TOKEN     "token"
#define MEDIA_LIVE_TOKEN_LEN 5
#define MEDIA_LIVE_TOKEN_MAX 128

#define MEDIA_LIVE_SESSION_MAX 64
#define MEDIA_LIVE_STREAM_MAX  64
#define MEDIA_LIVE_FILE_NAME_MAX 256
#define MEDIA_LIVE_URI_MAX     1024

typedef enum {
    ngx_media_live_session_start  = 0,
    ngx_media_live_session_stop   = 1
} ngx_media_live_session_status_t;


typedef struct {
    time_t                         start;
    time_t                         last;
    ngx_uint_t                     flux;
} ngx_media_live_session_info;

typedef struct {
    ngx_str_t                      name;
    ngx_str_t                      uri;
    ngx_str_t                      args;
    ngx_log_t                     *log;
    ngx_queue_t                    node;
} ngx_media_live_session_t;


typedef struct {
    ngx_queue_t                    free;
    ngx_queue_t                    used;
    ngx_pool_t                    *pool;
    ngx_log_t                     *log;
    ngx_event_t                    timer;
    ngx_shm_zone_t                *shm_zone;
} ngx_media_live_session_ctx_t;

typedef struct {
    ngx_str_t                       on_play;
    ngx_str_t                       on_play_done;
    ngx_msec_t                      timeout;
    ngx_buffer_cache_t*             session;
    ngx_media_live_session_ctx_t   *ctx;
} ngx_media_live_loc_conf_t;

typedef struct {
    ngx_buf_t                      *request;
    ngx_buf_t                      *response;
    ngx_peer_connection_t           peer;
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    ngx_url_t                       url;
    ngx_uint_t                      sendcount;
}ngx_media_live_report_ctx_t;


static char*     ngx_media_live_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_media_live_init_process(ngx_cycle_t *cycle);
static void      ngx_media_live_exit_process(ngx_cycle_t *cycle);
static void*     ngx_media_live_create_loc_conf(ngx_conf_t *cf);
static char*     ngx_media_live_cache_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void      ngx_media_live_check_session(ngx_event_t *ev);
static ngx_media_live_session_t* ngx_media_live_get_free_session(ngx_media_live_session_ctx_t* ctx,ngx_str_t* uri,ngx_str_t* args);
static void      ngx_media_live_report_session_status(ngx_media_live_session_t* s,ngx_media_live_session_status_t status,
                                                      ngx_str_t *url,ngx_media_live_session_info* info);
static ngx_int_t ngx_media_live_send_static_file(ngx_http_request_t *r,ngx_str_t *file);






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

    { ngx_string(NGX_MEDIA_LIVE_SESSION_CACHE),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE123,
      ngx_media_live_cache_command,
      NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_media_live_loc_conf_t, session),
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
ngx_media_live_hls_first_req(ngx_http_request_t *r)
{
    ngx_int_t                      rc;
    ngx_media_live_loc_conf_t     *conf;
    ngx_media_live_session_info    info;
    ngx_media_live_session_t      *s;
    u_char                        *last;
    ngx_chain_t                    out;
    ngx_buf_t                     *b;

    conf = ngx_http_get_module_loc_conf(r, ngx_media_live_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live module conf fail.");
        return NGX_HTTP_NOT_ALLOWED;
    }

     /* 1. check the license */
    ngx_uint_t count   = ngx_media_live_hls_get_cur_count(r);
    ngx_uint_t licesen = ngx_media_license_hls_channle();

    if (count >= licesen)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                      "hls session is full,count:[%d],license:[%d].",count,licesen);
        return NGX_HTTP_NOT_ALLOWED;
    }
    ngx_media_license_hls_count(count);


    s = ngx_media_live_get_free_session(conf->ctx,&r->uri,&r->args);
    if(NULL == s) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live free session fail.");
        return NGX_HTTP_NOT_ALLOWED;
    }
    ngx_media_live_report_session_status(s,ngx_media_live_session_start,&conf->on_play,NULL);

    /* add the session info to the share memory */
    info.flux  = 0;
    info.last  = ngx_time();
    info.start = ngx_time();

    ngx_buffer_cache_store(conf->session,s->name.data,(u_char*)&info,sizeof(ngx_media_live_session_info));

    /* send the respose */

    out.buf = NULL;

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = ngx_pcalloc(r->pool, MEDIA_LIVE_URI_MAX);
    if(b->pos == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    last = ngx_snprintf(b->pos,MEDIA_LIVE_URI_MAX,"#EXTM3U\r\n"
                                                  "#EXT-X-STREAM-INF:PROGRAM-ID=%V,BANDWIDTH=20480000\r\n"
                                                  "%V?%s=%V",
                                                  &s->name,
                                                  &r->uri,MEDIA_LIVE_TOKEN,&s->name);
    *last = '\0';
    b->last = last;

    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    r->header_only = 0;
    r->headers_out.status = NGX_HTTP_OK;
    r->keepalive   = 0;
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK) {
        ngx_http_finalize_request(r,rc);
        return NGX_DONE;
    }

    ngx_http_finalize_request(r,ngx_http_output_filter(r, &out));

    return NGX_DONE;
}
static ngx_int_t
ngx_media_live_hls_m3u8_req(ngx_http_request_t *r,ngx_str_t *m3u8,size_t fsize,u_char *token)
{
    ssize_t            ssize;
    ngx_file_t         file;
    size_t             size = fsize*2;
    ngx_buf_t         *buf;
    ngx_buf_t         *b;
    ngx_chain_t        out;
    u_char            *last;
    u_char            *begin;
    u_char            *end;

    ngx_memzero(&file, sizeof(ngx_file_t));
    file.name = *m3u8;
    file.log = r->connection->log;

    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);

    if (file.fd == NGX_INVALID_FILE) {

        ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno,
                      "failed to open the m3u8 file \"%V\".", &file.name);
        return NGX_HTTP_NOT_FOUND;
    }

    buf = ngx_create_temp_buf(r->pool,fsize);
    if (buf == NULL) {
        ngx_close_file(file.fd);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    b = ngx_create_temp_buf(r->pool,size);
    if (b == NULL) {
        ngx_close_file(file.fd);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ssize = ngx_read_file(&file,buf->start,fsize,0);

    if((ssize_t)fsize != ssize) {
        ngx_close_file(file.fd);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ngx_close_file(file.fd);

    buf->last = buf->start + fsize;
    /* parser the m3u8 playlist file and reconstruct response playlist */
    begin = buf->start;
    end   = buf->last;
    last = (u_char*)ngx_strstr(buf->start,"#EXTINF");
    if(NULL == last) {
        return NGX_HTTP_NOT_FOUND;
    }
    last = (u_char*)ngx_strchr(last,'\n');
    last += 1;

    while(NULL != last) {
        /* copy segment ts info */
        last = (u_char*)ngx_strchr(last,'\n');
        if(NULL == last) {
            break;
        }

        size = last - begin;
        b->last = ngx_cpymem(b->last, begin,size);
        /* copy segment ts info */
        b->last = ngx_snprintf(b->last,MEDIA_LIVE_URI_MAX, "?%s=%s\n",MEDIA_LIVE_TOKEN,token);
        last += 1;

        /* next ts segment info */
        begin = last;
        last = (u_char*)ngx_strstr(begin,"#EXTINF");
        if(NULL == last) {
            break;
        }
        last = (u_char*)ngx_strchr(last,'\n');
        last += 1;
    }

    /* copy the end info */
    if(begin < end) {
        size = end - begin;
        b->last = ngx_cpymem(b->last, begin,size);
    }

    /* send the response */

    *b->last = '\0';
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    r->header_only = 0;
    r->headers_out.status = NGX_HTTP_OK;
    r->keepalive   = 0;
    ngx_int_t rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK) {
        ngx_http_finalize_request(r,rc);
        return NGX_DONE;
    }

    ngx_http_finalize_request(r,ngx_http_output_filter(r, &out));

    return NGX_DONE;
}


static ngx_int_t
ngx_media_live_hls_handler(ngx_http_request_t *r)
{
    ngx_int_t                      rc;
    ngx_media_live_loc_conf_t     *conf;
    u_char                        *last;
    size_t                         root;
    ngx_str_t                      reqfile;
    ngx_file_info_t                fi;
    ngx_str_t                      arg;
    ngx_media_live_session_info   *info;
    ngx_str_t                      buffer;
    u_char                         key[MEDIA_LIVE_TOKEN_MAX];
    uint32_t                       token;


    if (r->uri.data[r->uri.len - 1] == '/') {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx media live request uri is invalid.");
        return NGX_DECLINED;
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_media_live_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live module conf fail.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    /* 1.discard request body, since we don't need it here */
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "discard the media live request body fail.");
        return rc;
    }

    /* 2.check the request file exsit */
    last = ngx_http_map_uri_to_path(r, &reqfile, &root, 0);
    if (NULL == last)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "the reuquest file path is not exist.");
        return NGX_HTTP_NOT_FOUND;
    }

    rc = ngx_file_info(reqfile.data, &fi);
    if (rc == NGX_FILE_ERROR)
    {
        return NGX_HTTP_NOT_FOUND;
    }
    if(!ngx_is_file(&fi))
    {
        return NGX_HTTP_NOT_FOUND;
    }

    /* 3.check token arg ,is that first request */
    if (0 == r->args.len) {
        return ngx_media_live_hls_first_req(r);
    }
    if (ngx_http_arg(r, (u_char *) MEDIA_LIVE_TOKEN,MEDIA_LIVE_TOKEN_LEN, &arg) != NGX_OK) {
        return ngx_media_live_hls_first_req(r);
    }

    ngx_memzero(key, MEDIA_LIVE_TOKEN_MAX);
    last = ngx_snprintf(key,MEDIA_LIVE_TOKEN_MAX-1,"%V", &arg);
    *last = '\0';

    /* 4.real file request with session token,so update the session info */
    if(!ngx_buffer_cache_fetch(conf->session,key,&buffer,&token)) {
        return NGX_HTTP_NOT_FOUND;
    }
    info = (ngx_media_live_session_info*)buffer.data;

    info->flux +=  ngx_file_size(&fi);
    info->last = ngx_time();

    if((5 < reqfile.len)
        &&(NULL != ngx_strstr(reqfile.data,MEDIA_LIVE_M3U8))) {
        return ngx_media_live_hls_m3u8_req(r,&reqfile,ngx_file_size(&fi),key);
    }
    /* ts file response direct */
    return ngx_media_live_send_static_file(r,&reqfile);
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

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_live_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }


    ngx_str_null(&conf->on_play);
    ngx_str_null(&conf->on_play_done);
    ngx_conf_init_msec_value(conf->timeout, NGX_CONF_UNSET_MSEC);
    conf->session  = NGX_CONF_UNSET_PTR;
    conf->ctx      = NULL;

    return conf;
}


static char *
ngx_media_live_cache_command(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	ngx_buffer_cache_t **cache = (ngx_buffer_cache_t **)((u_char*)conf + cmd->offset);
	ngx_str_t  *value;
	ssize_t size;
	time_t expiration;

	value = cf->args->elts;

	if (*cache != NGX_CONF_UNSET_PTR)
	{
		return "is duplicate";
	}

	if (ngx_strcmp(value[1].data, "off") == 0)
	{
		*cache = NULL;
		return NGX_CONF_OK;
	}

	if (cf->args->nelts < 3)
	{
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"size not specified in \"%V\"", &cmd->name);
		return NGX_CONF_ERROR;
	}

	size = ngx_parse_size(&value[2]);
	if (size == NGX_ERROR)
	{
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid size %V", &value[2]);
		return NGX_CONF_ERROR;
	}

	if (cf->args->nelts > 3)
	{
		expiration = ngx_parse_time(&value[3], 1);
		if (expiration == (time_t)NGX_ERROR)
		{
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid expiration %V", &value[3]);
			return NGX_CONF_ERROR;
		}
	}
	else
	{
		expiration = 0;
	}

	*cache = ngx_buffer_cache_create(cf, &value[1], size, expiration, &ngx_media_live_module);
	if (*cache == NULL)
	{
		ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"failed to create cache");
		return NGX_CONF_ERROR;
	}

	return NGX_CONF_OK;
}

static ngx_int_t
ngx_media_live_shm_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_slab_pool_t    *shpool;
    uint32_t           *nconn;

    if (data) {
        shm_zone->data = data;
        return NGX_OK;
    }

    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    nconn = ngx_slab_alloc(shpool, 4);
    if (nconn == NULL) {
        return NGX_ERROR;
    }

    *nconn = 0;

    shm_zone->data = nconn;

    return NGX_OK;
}


static char*
ngx_media_live_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t       *clcf;
    ngx_str_t                      *value;
    ngx_media_live_loc_conf_t      *mlconf;
    ngx_media_live_session_ctx_t   *ctx;
    ngx_pool_t                     *pool;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_live_hls_handler;/* default is hls handler */

    if(1 >= cf->args->nelts) {
        return NGX_CONF_OK;
    }
    value = cf->args->elts;
    if (ngx_strcasecmp(value[1].data, (u_char *) "hls") == 0)
	{
		clcf->handler = ngx_media_live_hls_handler;
	}
	else if (ngx_strcasecmp(value[1].data, (u_char *) "dash") == 0)
	{
		clcf->handler = ngx_media_live_dash_handler;
	}
	else
	{
		clcf->handler = ngx_media_live_hls_handler;/* default is hls handler */
	}

    mlconf = ngx_http_conf_get_module_loc_conf(cf, ngx_media_live_module);

    if (NULL == mlconf) {
        return NGX_CONF_ERROR;
    }

    pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);
    if (pool == NULL) {
        return NGX_CONF_ERROR;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_media_live_session_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    ctx->shm_zone = ngx_shared_memory_add(cf, &shm_name, ngx_pagesize * 2,
                                           &ngx_media_live_module);
    if (ctx->shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    ctx->shm_zone->init = ngx_media_live_shm_init;

    ngx_queue_init(&ctx->free);
    ngx_queue_init(&ctx->used);

    ctx->pool = pool;
    ctx->log = cf->log;

    mlconf->ctx = ctx;

    /* start the timer for check session */
    ctx->timer.handler = ngx_media_live_check_session;
    ctx->timer.log     = ctx->log;
    ctx->timer.data    = mlconf;

    ngx_add_timer(&ctx->timer,mlconf->timeout);

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_media_live_init_process(ngx_cycle_t *cycle)
{
    return NGX_OK;
}

static void
ngx_media_live_exit_process(ngx_cycle_t *cycle)
{
    /* destory the session manage */
    return ;
}
static void
ngx_media_live_check_session(ngx_event_t *ev)
{
    ngx_queue_t                  *q,*t;
    ngx_media_live_session_t     *s;
    ngx_str_t                     b;
    uint32_t                      token;
    ngx_media_live_session_info  *info;
    ngx_media_live_session_ctx_t *ctx;
    ngx_media_live_loc_conf_t    *conf
            = (ngx_media_live_loc_conf_t*)ev->data;

    ctx = conf->ctx;

    time_t now = ngx_time();
    time_t timeout = (time_t)(conf->timeout/1000);

    for (q = ngx_queue_head(&ctx->used);
         q != ngx_queue_sentinel(&ctx->used);) {
        t = ngx_queue_next(q);
        s = ngx_queue_data(q, ngx_media_live_session_t, node);
        if(!ngx_buffer_cache_fetch(conf->session,s->name.data,&b,&token)) {
            /* the share cache is not exist,so free the session */
            ngx_media_live_report_session_status(s,ngx_media_live_session_stop,&conf->on_play_done,NULL);
            ngx_queue_remove(q);
            ngx_queue_insert_tail(&ctx->free,q);
        }
        info = (ngx_media_live_session_info*)b.data;
        if((now > info->last)&&(timeout <(now - info->last))) {
            /* the session is timeout ,so free the session */
            ngx_media_live_report_session_status(s,ngx_media_live_session_stop,&conf->on_play_done,info);
            ngx_queue_remove(q);
            ngx_queue_insert_tail(&ctx->free,q);
        }
        q = t;
    }



    /* next timer for check */
    ctx->timer.handler = ngx_media_live_check_session;
    ctx->timer.log     = ctx->log;
    ctx->timer.data    = conf;

    ngx_add_timer(&ctx->timer,conf->timeout);
}

static ngx_media_live_session_t*
ngx_media_live_get_free_session(ngx_media_live_session_ctx_t* ctx,ngx_str_t* uri,ngx_str_t* args)
{
    ngx_media_live_session_t   *session = NULL;
    ngx_queue_t                *node    = NULL;
    uint32_t                   *nconn;
    uint32_t                    n;
    ngx_shm_zone_t             *shm_zone;
    ngx_slab_pool_t            *shpool;
    u_char                     *last;

    if(ngx_queue_empty(&ctx->free)) {
        session = ngx_pcalloc(ctx->pool, sizeof(ngx_media_live_session_t));
        node   = &session->node;
        session->name.data = ngx_pcalloc(ctx->pool, MEDIA_LIVE_SESSION_MAX);
        session->name.len  = MEDIA_LIVE_SESSION_MAX;
        session->log = ctx->log;
        ngx_str_null(&session->args);
        ngx_str_null(&session->uri);
    }
    else {
        node = ngx_queue_head(&ctx->free);
        ngx_queue_remove(node);
        session = ngx_queue_data(node, ngx_media_live_session_t, node);

        if(NULL != session->args.data) {
            ngx_pfree(ctx->pool,session->args.data);
            ngx_str_null(&session->args);
        }
        if(NULL != session->uri.data) {
            ngx_pfree(ctx->pool,session->uri.data);
            ngx_str_null(&session->uri);
        }
    }



    shm_zone = ctx->shm_zone;
    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    nconn = shm_zone->data;

    ngx_shmtx_lock(&shpool->mutex);
    n = (*nconn)++;
    ngx_shmtx_unlock(&shpool->mutex);

    last = ngx_snprintf(session->name.data,MEDIA_LIVE_SESSION_MAX,"%L", n);
    session->name.len = last - session->name.data;
    *last = '\0';

    if((NULL != uri->data)&&(0 < uri->len)) {
        session->uri.data = ngx_pcalloc(ctx->pool, uri->len + 1);
        session->uri.len  = args->len;
        last = ngx_cpymem(session->uri.data,uri->data,uri->len);
        *last = '\0';
    }

    if((NULL != args->data)&&(0 < args->len)) {
        session->args.data = ngx_pcalloc(ctx->pool, args->len + 1);
        session->args.len  = args->len;
        last = ngx_cpymem(session->args.data,args->data,args->len);
        *last = '\0';
    }

    ngx_queue_insert_tail(&ctx->used,node);

    return session;
}

static void
ngx_media_live_report_write_dummy_handler(ngx_event_t *ev)
{
    ngx_log_debug0(NGX_LOG_DEBUG, ev->log, 0,
                   "media live rreport http dummy handler");
}

static void
ngx_media_live_report_write_handler(ngx_event_t *wev)
{
    ssize_t                      n, size;
    ngx_connection_t             *c;
    ngx_media_live_report_ctx_t  *ctx;

    c = wev->data;
    ctx  = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG, wev->log, 0,
                   "media live report http write handler");

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_ERR, wev->log, NGX_ETIMEDOUT,
                      "media live report http server timed out");

        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        return;
    }

    size = ctx->request->last - ctx->request->pos;

    n = ngx_send(c, ctx->request->pos, size);
    ctx->sendcount++;

    if (n == NGX_ERROR) {
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "media live send to peer fail!");
        return;
    }

    if (n > 0) {
        ctx->request->pos += n;

        if (n == size) {
            wev->handler = ngx_media_live_report_write_dummy_handler;

            if (wev->timer_set) {
                ngx_del_timer(wev);
            }

            if (ngx_handle_write_event(wev, 0) != NGX_OK) {
                ngx_close_connection(c);
                ngx_destroy_pool(ctx->pool);
                ngx_log_error(NGX_LOG_DEBUG, wev->log, 0, "close the handle by the write event");
            }

            return;
        }
    }

    if (5 < ctx->sendcount) {
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "media live send to peer try max times!");
        return;
    }

    if (!wev->timer_set) {
        ngx_add_timer(wev, 1000);
    }
}

static void
ngx_media_live_report_read_handler(ngx_event_t *rev)
{
    ssize_t                      n, size;
    ngx_connection_t             *c;
    ngx_media_live_report_ctx_t  *ctx;

    c = rev->data;
    ctx  = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0,
                   "trans report http read handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_ERR, rev->log, NGX_ETIMEDOUT,
                      "trans report http server timed out");
        c->timedout = 1;
        if (rev->timer_set) {
            ngx_del_timer(rev);
        }
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        return;
    }

    if (ctx->response == NULL) {
        ctx->response = ngx_create_temp_buf(ctx->pool, 1024);
        if (ctx->response == NULL) {
            ngx_close_connection(c);
            ngx_destroy_pool(ctx->pool);
            ngx_log_error(NGX_LOG_DEBUG, rev->log, 0, "close the handle by the recv event");
            return;
        }
    }

    size = ctx->response->end - ctx->response->last;

    n = ngx_recv(c, ctx->response->pos, size);

    if (n > 0) {
        ctx->response->last += n;
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        ngx_log_error(NGX_LOG_DEBUG, rev->log, 0, "close the handle by the recv event");
        return;
    }

    if (n == NGX_AGAIN) {
        return;
    }
    ngx_close_connection(c);
    ngx_destroy_pool(ctx->pool);
    ngx_log_error(NGX_LOG_DEBUG, rev->log, 0, "close the handle by the recv event");
}

static ngx_buf_t *
ngx_media_live_report_create_request(ngx_media_live_report_ctx_t* ctx,ngx_media_live_session_t* s,
                                     ngx_media_live_session_status_t status,ngx_media_live_session_info* info)
{
    xmlDocPtr                       doc         = NULL;/* document pointer */
    xmlNodePtr                      report_node = NULL;
    xmlNodePtr                      session     = NULL;
    xmlChar                        *xmlbuff     = NULL;
    int                             buffersize  = 0;
    u_char                         *last        = NULL;

    size_t     len;
    ngx_buf_t  *b;
    u_char buf[128];
    ngx_memzero(&buf, 128);


    /* Creates a new document, a node and set it as a root node*/
    doc = xmlNewDoc(BAD_CAST "1.0");
    report_node = xmlNewNode(NULL, BAD_CAST "report");
    xmlNewProp(report_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlDocSetRootElement(doc, report_node);
    session   = xmlNewNode(NULL, BAD_CAST "session");
    xmlNewProp(session, BAD_CAST "session", BAD_CAST s->name.data);

    if(status == ngx_media_live_session_start) {
        xmlNewProp(session, BAD_CAST "status", BAD_CAST "on_play");
    }
    else if(status == ngx_media_live_session_stop) {
        xmlNewProp(session, BAD_CAST "status", BAD_CAST "on_play_done");
    }
    else {
        xmlNewProp(session, BAD_CAST "status", BAD_CAST "invalid");
    }

    xmlNewProp(session, BAD_CAST "url", BAD_CAST s->uri.data);

    if(NULL != info) {
        ngx_media_time2string(buf,info->start);
        xmlNewProp(session, BAD_CAST "start", BAD_CAST buf);
        ngx_media_time2string(buf,info->last);
        xmlNewProp(session, BAD_CAST "end", BAD_CAST buf);
        last = ngx_snprintf(buf, 128, "%Lbyte", info->flux);
        xmlNewProp(session, BAD_CAST "flux", BAD_CAST buf);
    }
    xmlAddChild(report_node, session);


    xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
    ngx_memzero(&buf, 128);
    last = ngx_snprintf(buf, 128, "%d", buffersize);
    *last = '\0';


    len = sizeof("POST ") - 1 + ctx->url.uri.len + sizeof(" HTTP/1.1" CRLF) - 1
          + sizeof("Host: ") - 1 + ctx->url.host.len + sizeof(CRLF) - 1
          + sizeof("User-Agent: AllMedia") - 1 + sizeof(CRLF) - 1
          + sizeof("Connection: close") - 1 + sizeof(CRLF) - 1
          + sizeof("Content-Type: application/xml") - 1 + sizeof(CRLF) - 1
          + sizeof("Content-Length: ") - 1 + ngx_strlen(buf) + sizeof(CRLF) - 1
          + sizeof(CRLF) - 1
          + buffersize;

    b = ngx_create_temp_buf(ctx->pool, len);
    if (b == NULL) {
        ngx_log_error(NGX_LOG_EMERG, s->log, 0, "media live create temp buf fail!");
        return NULL;
    }

    b->last = ngx_cpymem(b->last, "POST ", sizeof("POST ") - 1);
    b->last = ngx_copy(b->last, ctx->url.uri.data, ctx->url.uri.len);
    b->last = ngx_cpymem(b->last, " HTTP/1.1" CRLF,
                         sizeof(" HTTP/1.1" CRLF) - 1);

    b->last = ngx_cpymem(b->last, "Host: ", sizeof("Host: ") - 1);
    b->last = ngx_copy(b->last, ctx->url.host.data,
                         ctx->url.host.len);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "User-Agent: AllMedia", sizeof("User-Agent: AllMedia") - 1);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "Connection: close", sizeof("Connection: close") - 1);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "Content-Type: application/xml", sizeof("Content-Type: application/xml") - 1);
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, "Content-Length: ", sizeof("Content-Length: ") - 1);
    b->last = ngx_copy(b->last, buf,ngx_strlen(buf));
    *b->last++ = CR; *b->last++ = LF;


    /* add "\r\n" at the header end */
    *b->last++ = CR; *b->last++ = LF;

    b->last = ngx_cpymem(b->last, xmlbuff, buffersize);

    xmlFree(xmlbuff);
    xmlFreeDoc(doc);
    xmlCleanupParser();

    return b;
}

static void
ngx_media_live_report_parse_report_url(ngx_media_live_report_ctx_t* ctx,ngx_media_live_session_t* s,ngx_str_t *url)
{
    ngx_str_t                new_url;
    u_char                  *last;
    size_t                   size;

    ngx_memzero(&ctx->url, sizeof(ngx_url_t));
    new_url.data = url->data;
    new_url.len  = url->len;

    if((NULL != s->args.data)&&(0 < s->args.len)) {
        size = url->len + s->args.len + 1;
        new_url.data = ngx_pcalloc(ctx->pool, size);
        new_url.len  = size;

        if(NULL != ngx_strchr(url->data,'?')) {
            if('?' == s->args.data[0]) {
                s->args.data[0] = '&';
                last = ngx_snprintf(new_url.data,size,"%V%V", url,&s->args);
            }
            else if('&' == s->args.data[0]) {
                last = ngx_snprintf(new_url.data,size,"%V%V", url,&s->args);
            }
            else {
                last = ngx_snprintf(new_url.data,size,"%V&%V", url,&s->args);
            }
        }
        else {
            if('?' == s->args.data[0]) {
                last = ngx_snprintf(new_url.data,size,"%V%V", url,&s->args);
            }
            else if('&' == s->args.data[0]) {
                s->args.data[0] = '?';
                last = ngx_snprintf(new_url.data,size,"%V%V", url,&s->args);
            }
            else {
                last = ngx_snprintf(new_url.data,size,"%V?%V", url,&s->args);
            }
        }
        *last = '\0';
    }

    /* append the args and new url */

    ctx->url.url = new_url;
    ctx->url.no_resolve = 1;
    ctx->url.uri_part = 1;

    if (ngx_strncmp(ctx->url.url.data, "http://", 7) == 0) {
        ctx->url.url.len -= 7;
        ctx->url.url.data += 7;
    }

    if (ngx_parse_url(ctx->pool, &ctx->url) != NGX_OK) {
         if (ctx->url.err) {
            ngx_log_error(NGX_LOG_ERR, s->log, 0,
                          "%s in task \"%V\"", ctx->url.err, &ctx->url.url);
        }
        return ;
    }
    return;
}


static void
ngx_media_live_report_session_status(ngx_media_live_session_t* s,ngx_media_live_session_status_t status,
                                     ngx_str_t *url,ngx_media_live_session_info* info)
{
    ngx_int_t                      rc;
    ngx_media_live_report_ctx_t   *ctx;
    ngx_pool_t                    *pool = NULL;
    if((NULL != url->data)||(0 == url->len)) {
        return;
    }


    pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, s->log);
    if (pool == NULL) {
        return;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_media_live_report_ctx_t));
    if (ctx == NULL) {
        ngx_destroy_pool(pool);
        return;
    }

    ctx->pool      = pool;
    ctx->log       = s->log;
    ctx->sendcount = 0;

    /* parser the url and append the url args */
    ngx_media_live_report_parse_report_url(ctx,s,url);

    ctx->request = ngx_media_live_report_create_request(ctx,s,status,info);
    if (ctx->request == NULL) {
        ngx_destroy_pool(pool);
        return;
    }

    ctx->peer.sockaddr = ctx->url.addrs->sockaddr;
    ctx->peer.socklen = ctx->url.addrs->socklen;
    ctx->peer.name = &ctx->url.addrs->name;
    ctx->peer.get = ngx_event_get_peer;
    ctx->peer.log = s->log;
    ctx->peer.log_error = NGX_ERROR_ERR;

    rc = ngx_event_connect_peer(&ctx->peer);

    if (rc != NGX_OK && rc != NGX_AGAIN ) {
        if (ctx->peer.connection) {
            ngx_close_connection(ctx->peer.connection);
        }
        ngx_log_error(NGX_LOG_EMERG, s->log, 0, "media live report connect to peer fail!");
        ngx_destroy_pool(pool);
        return;
    }

    ctx->peer.connection->data = ctx;
    ctx->peer.connection->pool = ctx->pool;

    ctx->peer.connection->read->handler = ngx_media_live_report_read_handler;
    //ctx->peer.connection->read->data = ctx;
    ctx->peer.connection->write->handler = ngx_media_live_report_write_handler;
    //ctx->peer.connection->write->data = ctx;

    //ngx_msleep(100);


    ngx_add_timer(ctx->peer.connection->read, 5000);
    ngx_add_timer(ctx->peer.connection->write, 1000);

    /* send the request direct */
    if (rc == NGX_OK ) {
        ngx_media_live_report_write_handler(ctx->peer.connection->write);
    }
    return;
}


static ngx_int_t
ngx_media_live_send_static_file(ngx_http_request_t *r,ngx_str_t *file)
{
    u_char                    *last, *location;
    size_t                     len;
    ngx_int_t                  rc;
    ngx_uint_t                 level;
    ngx_log_t                 *log;
    ngx_buf_t                 *b;
    ngx_chain_t                out;
    ngx_open_file_info_t       of;
    ngx_http_core_loc_conf_t  *clcf;


    log = r->connection->log;

    /*
     * ngx_http_map_uri_to_path() allocates memory for terminating '\0'
     * so we do not need to reserve memory for '/' for possible redirect
     */


    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0,
                   "http filename: \"%s\"", file->data);

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ngx_memzero(&of, sizeof(ngx_open_file_info_t));

    of.read_ahead = clcf->read_ahead;
    of.directio = clcf->directio;
    of.valid = clcf->open_file_cache_valid;
    of.min_uses = clcf->open_file_cache_min_uses;
    of.errors = clcf->open_file_cache_errors;
    of.events = clcf->open_file_cache_events;

    if (ngx_http_set_disable_symlinks(r, clcf, file, &of) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_open_cached_file(clcf->open_file_cache, file, &of, r->pool)
        != NGX_OK)
    {
        switch (of.err) {

        case 0:
            return NGX_HTTP_INTERNAL_SERVER_ERROR;

        case NGX_ENOENT:
        case NGX_ENOTDIR:
        case NGX_ENAMETOOLONG:

            level = NGX_LOG_ERR;
            rc = NGX_HTTP_NOT_FOUND;
            break;

        case NGX_EACCES:
#if (NGX_HAVE_OPENAT)
        case NGX_EMLINK:
        case NGX_ELOOP:
#endif

            level = NGX_LOG_ERR;
            rc = NGX_HTTP_FORBIDDEN;
            break;

        default:

            level = NGX_LOG_CRIT;
            rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            break;
        }

        if (rc != NGX_HTTP_NOT_FOUND || clcf->log_not_found) {
            ngx_log_error(level, log, of.err,
                          "%s \"%s\" failed", of.failed, file->data);
        }

        return rc;
    }

    r->root_tested = !r->error_page;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "http static fd: %d", of.fd);

    if (of.is_dir) {

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "http dir");

        ngx_http_clear_location(r);

        r->headers_out.location = ngx_list_push(&r->headers_out.headers);
        if (r->headers_out.location == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        len = r->uri.len + 1;

        if (!clcf->alias && clcf->root_lengths == NULL && r->args.len == 0) {
            location = file->data + clcf->root.len;

        } else {
            if (r->args.len) {
                len += r->args.len + 1;
            }

            location = ngx_pnalloc(r->pool, len);
            if (location == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            last = ngx_copy(location, r->uri.data, r->uri.len);

            *last = '/';

            if (r->args.len) {
                *++last = '?';
                ngx_memcpy(++last, r->args.data, r->args.len);
            }
        }

        r->headers_out.location->hash = 1;
        ngx_str_set(&r->headers_out.location->key, "Location");
        r->headers_out.location->value.len = len;
        r->headers_out.location->value.data = location;

        return NGX_HTTP_MOVED_PERMANENTLY;
    }

#if !(NGX_WIN32) /* the not regular files are probably Unix specific */

    if (!of.is_file) {
        ngx_log_error(NGX_LOG_CRIT, log, 0,
                      "\"%s\" is not a regular file", file->data);

        return NGX_HTTP_NOT_FOUND;
    }

#endif

    if (r->method == NGX_HTTP_POST) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    log->action = "sending response to client";

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = of.size;
    r->headers_out.last_modified_time = of.mtime;

    if (ngx_http_set_etag(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (r != r->main && of.size == 0) {
        return ngx_http_send_header(r);
    }

    r->allow_ranges = 1;

    /* we need to allocate all before the header would be sent */

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));
    if (b->file == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    b->file_pos = 0;
    b->file_last = of.size;

    b->in_file = b->file_last ? 1: 0;
    b->last_buf = (r == r->main) ? 1: 0;
    b->last_in_chain = 1;

    b->file->fd = of.fd;
    b->file->name = *file;
    b->file->log = log;
    b->file->directio = of.is_directio;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


ngx_uint_t ngx_media_live_hls_get_cur_count(ngx_http_request_t *r)
{
    ngx_media_live_loc_conf_t  *conf;
    uint32_t                   *nconn;
    ngx_shm_zone_t             *shm_zone;
    ngx_slab_pool_t            *shpool;
    ngx_uint_t                  n;
    conf = ngx_http_get_module_loc_conf(r, ngx_media_live_module);
    if(NULL == conf) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "get the media live module conf fail.");
        return 0;
    }
    if(NULL == conf->ctx) {
        return 0;
    }
    if(NULL == conf->ctx->shm_zone) {
        return 0;
    }
    shm_zone = conf->ctx->shm_zone;
    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    nconn = shm_zone->data;

    ngx_shmtx_lock(&shpool->mutex);
    n = *nconn;
    ngx_shmtx_unlock(&shpool->mutex);
    return n;
}




