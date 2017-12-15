/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_live_session_module.c
 Version    : V 1.0.0
 Date       : 2016-04-28
 Author     : hexin H.kernel
 Modify     :
            1.2016-04-28: create
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
#include "ngx_media_live_session_module.h"
#include "ngx_media_include.h"
#include "ngx_media_live_cache_module.h"
#include "hash.h"
#include "shm_hashtable.h"
#include "shmcache.h"



#define NGX_MEDIA_LIVE_SESSION_TIMEOUT       30000
#define NGX_MEDIA_LIVE_SESSION_CONF           "../conf/shmcache.conf"
#define NGX_MEDIA_LIVE_CONF_FILE_LEN 128


typedef enum ngx_media_live_session_status_s{
    ngx_media_live_session_status_free     = 0,
    ngx_media_live_session_status_used     = 1
} live_session_status_t;

typedef enum ngx_media_live_type_t{
    ngx_media_live_hls     = 0,
    ngx_media_live_dash    = 1,
    ngx_media_live_end
}live_session_type_t;

/* session ctx,store to the share memory */
typedef struct {
    live_session_status_t     status;
    live_session_type_t       type;
    time_t                    start;
    time_t                    last;
    ngx_uint_t                flux;
    ngx_uint_t                ttl;
} ngx_media_live_session_ctx_t;

typedef struct {
    u_char                    name[SHMCACHE_MAX_KEY_SIZE]; /* processid-sessionid-streamid */
    ngx_uint_t                sessionid;
    ngx_msec_t                timeout;
    ngx_str_t                 on_play;
    ngx_str_t                 on_play_done;
    ngx_pool_t               *pool;
    ngx_log_t                *log;
    ngx_queue_t               queue_node;
} ngx_media_live_session_t;


typedef struct {
    ngx_uint_t                 count;       /* max session count  */
    ngx_str_t                  conf;        /* share memory config file */
    ngx_queue_t                free;        /* free session queue */
    ngx_queue_t                used;        /* uese session queue */
    ngx_pool_t                *pool;        /* memory pool for session */
    struct shmcache_context    cache;       /* share memory cache */
    ngx_event_t                timer;       /* check session timeout timer */
} ngx_media_live_session_core_conf_t;


static ngx_int_t ngx_media_live_session_init_process(ngx_cycle_t *cycle);
static void      ngx_media_live_session_exit_process(ngx_cycle_t *cycle);
static void      ngx_media_live_session_exit_master(ngx_cycle_t *cycle);
static void *    ngx_media_live_session_create_conf(ngx_cycle_t *cycle);
static char *    ngx_media_live_session_init_conf(ngx_cycle_t *cycle, void *conf);
static void      ngx_media_live_session_check(ngx_event_t *ev);

static ngx_int_t ngx_media_live_session_exist(ngx_str_t* name);
static ngx_int_t ngx_media_live_session_create(ngx_str_t* name,ngx_str_t* play,ngx_str_t*play_done,ngx_uint_t ttl);
static ngx_int_t ngx_media_live_session_update(ngx_str_t* name,ngx_uint_t flux);
static void      ngx_media_live_session_report(ngx_media_live_session_t *s,live_session_status_t status);



static ngx_command_t  ngx_media_live_session_commands[] = {

     { ngx_string(NGX_MEDIA_LIVE_SESSION_COUNT),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_media_live_session_core_conf_t, count),
      NULL },

     { ngx_string(NGX_MEDIA_LIVE_SESSION_CACHE),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_media_live_session_core_conf_t, conf),
      NULL },


      ngx_null_command
};


static ngx_core_module_t  ngx_media_live_session_module_ctx = {
    ngx_string("media_live_cache"),
    ngx_media_live_session_create_conf,
    ngx_media_live_session_init_conf

};


ngx_module_t  ngx_media_live_session_module = {
    NGX_MODULE_V1,
    &ngx_media_live_session_module_ctx,      /* module context */
    ngx_media_live_session_commands,         /* module directives */
    NGX_CORE_MODULE,                         /* module type */
    NULL,                                    /* init master */
    NULL,                                    /* init module */
    ngx_media_live_session_init_process,     /* init process */
    NULL,                                    /* init thread */
    NULL,                                    /* exit thread */
    ngx_media_live_session_exit_process,     /* exit process */
    ngx_media_live_session_exit_master,      /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_media_live_session_create_conf(ngx_cycle_t *cycle)
{
    ngx_media_live_session_core_conf_t  *conf;

    conf = ngx_pcalloc(cycle->pool, sizeof(ngx_media_live_session_core_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->count= NGX_CONF_UNSET;
    ngx_str_null(&conf->conf);
    conf->cache = NULL;

    return conf;
}
static char *
ngx_media_live_session_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_media_live_session_core_conf_t  *ccf = conf;

    //ngx_conf_init_value(ccf->count, 0);
    ngx_str_set(&ccf->conf, NGX_MEDIA_LIVE_CONF_FILE_DEFAULT);
    ccf->cache = NULL;
    ngx_queue_init(&ccf->free);
    ngx_queue_init(&ccf->used);
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_media_live_session_init_process(ngx_cycle_t *cycle)
{

    ngx_media_live_session_core_conf_t  *conf;
    ngx_uint_t                           i = 0;
    ngx_str_t                            name;
    ngx_pool_t                          *pool = NULL;
    ngx_media_live_session_t            *s = NULL;
    size_t                               size = sizeof(ngx_media_live_session_t);
    u_char conf_file[NGX_MEDIA_LIVE_CONF_FILE_LEN];

    ngx_str_set(&name, NGX_MEDIA_LIVCE_SESSION_CACHE_NAME);

    conf = (ngx_media_live_session_core_conf_t *)
                ngx_get_conf(cycle->conf_ctx, ngx_media_live_session_module);
    if(NULL == conf) {
        return NGX_ERROR;
    }

    if((0 == conf->count)||(NGX_CONF_UNSET == conf->count) {
        return NGX_OK;
    }
    pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cycle->log);
    if(NULL == pool) {
         return NGX_ERROR;
    }
    conf->pool = pool;

    ngx_memzero(&conf_file[0], NGX_MEDIA_LIVE_CONF_FILE_LEN);
    ngx_snprintf(&conf_file[0],NGX_MEDIA_LIVE_CONF_FILE_LEN,"%V", &conf->conf);

    if(0 != shmcache_init_from_file(&conf->cache,(const char*)&conf_file[0])) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_live_session_module: init shmcache fail.");
        return NGX_ERROR;
    }

    /* create the session for this process */
    for(i = 0;i < conf->count;i++) {
        s = (ngx_media_live_session_t*)ngx_pcalloc(pool,size);
        if(NULL == s) {
            return NGX_ERROR;
        }
        s->log = conf->log;
        s->pool = pool;
        s->sessionid = i;
        s->timeout   = NGX_MEDIA_LIVE_SESSION_TIMEOUT;
        ngx_str_null(&s->on_play);
        ngx_str_null(&s->on_play_done);
        ngx_queue_insert_tail(&conf->free, s->queue_node);
    }

    /* start the session timeout check timer */
    ngx_memzero(&conf->timer, sizeof(ngx_event_t));
    conf->timer.handler = ngx_media_live_session_check;
    conf->timer.log     = conf->log;
    conf->timer.data    = conf;

    ngx_add_timer(&conf->timer,5000);

    return NGX_OK;
}

static void
ngx_media_live_session_exit_process(ngx_cycle_t *cycle)
{
    /* destory the session manage */
    ngx_media_live_session_core_conf_t  *conf;
    ngx_uint_t                         i = 0;
    ngx_media_live_session_ctx_t        *ctx = NULL;

    conf = (ngx_media_live_session_core_conf_t *)
                ngx_get_conf(cycle->conf_ctx, ngx_media_live_session_module);
    if(NULL == conf) {
        return ;
    }
    if((0 == conf->count)||(NGX_CONF_UNSET == conf->count) {
        return ;
    }
    ngx_del_timer(&conf->timer);
    if(NULL != conf->pool) {
        ngx_destroy_pool(conf->pool);
    }
    shmcache_destroy(&conf->cache);

    return ;
}


static void
ngx_media_live_session_exit_master(ngx_cycle_t *cycle)
{
    /* destory the session manage */
    ngx_media_live_session_core_conf_t  *conf;
    struct shmcache_context context;
    u_char conf_file[NGX_MEDIA_LIVE_CONF_FILE_LEN];
    ngx_keyval_t                      *kv = NULL;
    ngx_uint_t                         i = 0;

    conf = (ngx_media_live_session_core_conf_t *)
                ngx_get_conf(cycle->conf_ctx, ngx_media_live_session_module);
    if(NULL == conf) {
        return ;
    }
    if((0 == conf->count)||(NGX_CONF_UNSET == conf->count) {
        return ;
    }

    /* mutil-process use the share memory to cache the live stream */
    ngx_memzero(&conf_file[0], NGX_MEDIA_LIVE_CONF_FILE_LEN);
    ngx_snprintf(&conf_file[0],NGX_MEDIA_LIVE_CONF_FILE_LEN,"%V", &conf->conf);

    if (0 !=shmcache_init_from_file_ex(&context,(const char*)&conf_file[0], false, false))
    {
        continue ;
    }

    if (0 == shmcache_remove_all(&context)) {
        ngx_log_debug(NGX_LOG_DEBUG, cycle->log, 0, "ngx_media_live_cache_module: all share live media memory segments are removed.");
    }

    return ;
}

static void
ngx_media_live_session_report(ngx_media_live_session_t *s,live_session_status_t status)
{
    if((ngx_media_live_session_status_used == status)
        &&((NULL == s->on_play.data)||(0 == s->on_play.len))) {
        return;
    }

    if((ngx_media_live_session_status_free == status)
        &&((NULL == s->on_play_done.data)||(0 == s->on_play_done.len))) {
        return;
    }
}

static void
ngx_media_live_session_check(ngx_event_t *ev)
{
    ngx_queue_t                         *q  = NULL;
    ngx_queue_t                         *p  = NULL;
    ngx_media_live_session_t            *s  = NULL;
    ngx_media_live_session_ctx_t        *sc = NULL;
    ngx_media_live_session_core_conf_t* conf
            = (ngx_media_live_session_core_conf_t*)ev->data;
    ngx_str_t                           key;
    ngx_uint_t                          len;

    time_t now   = ngx_time();

    for (q = ngx_queue_head(&conf->used);
         q != ngx_queue_sentinel(&conf->used);
         q = ngx_queue_next(q))
    {
        s = (ngx_media_live_session_t *)ngx_queue_data(q,ngx_media_live_session_t,queue_node);

        /* find the info from the cache */
        key.data = &s->name[0];
        key.len  = ngx_strlen(key.data);
        if(NGX_OK != ngx_media_live_cache_read(conf->cache,&key,&sc,&len)) {
            ngx_media_live_session_report(s,ngx_media_live_session_status_free);
            /* this node is node in the cache,normal will not be happen */
            p = ngx_queue_prev(q);
            ngx_queue_remove(q);
            q = p;
            continue;
        }

        /* check the timeout */
        if(sc->last > now) {
            continue;
        }

        if((now - sc->last) <= s->timeout) {
            continue;
        }
        /* the session is timetoue ,report stop and release */
        ngx_media_live_session_report(s,ngx_media_live_session_status_free);
        /* free the share cache */
        ngx_media_live_cache_delete(conf->cache,&key);
        /* delete and push to the free q */
        p = ngx_queue_prev(q);
        ngx_queue_remove(q);
        ngx_queue_insert_tail(&conf->free, q);
        q = p;
    }

    ngx_add_timer(&conf->timer, 5000);
}


static ngx_int_t
ngx_media_live_session_exist(ngx_str_t* name)
{
    ngx_media_live_session_ctx_t        *sc = NULL;
    struct shmcache_key_info shm_key;
    struct shmcache_value_info value;
    ngx_media_live_session_core_conf_t* conf
            = (ngx_media_live_session_core_conf_t*)ev->data;
    ngx_uint_t                          len;

    conf = (ngx_media_live_session_core_conf_t *)
                ngx_get_conf(ngx_cycle->conf_ctx, ngx_media_live_session_module);
    if(NULL == conf) {
        return NGX_ERROR;
    }
    shm_key.data = name->data;
    shm_key.len  = name->len;
    if( 0 != shmcache_get(&conf->cache,&shm_key,&value)) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "ngx_media_live_session_module: read fail.");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_media_live_session_create(ngx_str_t* name,ngx_str_t* play,ngx_str_t*play_done,ngx_uint_t ttl)
{
    ngx_queue_t                         *q  = NULL;
    ngx_media_live_session_t            *s  = NULL;
    ngx_media_live_session_ctx_t         sc;
    ngx_media_live_session_core_conf_t* conf= NULL;
    ngx_str_t                           key;
    ngx_uint_t                          len;
    u_char                             *last= NULL;
    struct shmcache_key_info            shm_key;

    len = sizeof(ngx_media_live_session_ctx_t);

    conf = (ngx_media_live_session_core_conf_t *)
                ngx_get_conf(ngx_cycle->conf_ctx, ngx_media_live_session_module);
    if(NULL == conf) {
        return NGX_ERROR;
    }

    if(ngx_queue_empty(&conf->free)) {
        return NGX_ERROR
    }

    q = ngx_queue_head(&conf->free);
    ngx_queue_remove(q);

    s = (ngx_media_live_session_t *)ngx_queue_data(q,ngx_media_live_session_t,queue_node);

    ngx_snprintf(s->name,SHMCACHE_MAX_KEY_SIZE,"%V", name);

    if((NULL != s->on_play.data)&&(0 < s->on_play.len)) {
        ngx_pfree(s->pool,s->on_play.data);
        ngx_str_null(&s->on_play);
    }

    if((NULL != s->on_play_done.data)&&(0 < s->on_play_done.len)) {
        ngx_pfree(s->pool,s->on_play_done.data);
        ngx_str_null(&s->on_play_done);
    }

    if(NULL != play) {
        s->on_play.data = ngx_palloc(s->pool,play->len+1);
        last = ngx_cpymem(s->on_play.data,play->data,play->len);
        *last = '\0';
        s->on_play.len = play->len;
    }

    if(NULL != play_done) {
        s->on_play_done.data = ngx_palloc(s->pool,play_done->len+1);
        last = ngx_cpymem(s->on_play_done.data,play_done->data,play_done->len);
        *last = '\0';
        s->on_play_done.len = play_done->len;
    }
    s->timeout = ttl;

    ngx_queue_insert_after(&conf->used,q);
    sc.status = ngx_media_live_session_status_used;
    sc.type   = ngx_media_live_hls;
    sc.start  = ngx_time();
    sc.last   = ngx_time();
    sc.ttl    = ttl;
    sc.flux   = 0;

    shm_key.data = key->data;
    shm_key.len  = key->len;

    if( 0 != shmcache_set(&conf->cache,&shm_key,(const u_char*)&sc,len,ttl)) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "ngx_media_live_session_module: write fail.");
        return NGX_ERROR;
    }

    return NGX_OK;
}
static ngx_int_t
ngx_media_live_session_update(ngx_str_t* name,ngx_uint_t flux)
{
    ngx_queue_t                         *q  = NULL;
    ngx_media_live_session_t            *s  = NULL;
    ngx_media_live_session_ctx_t        *sc = NULL;
    ngx_media_live_session_ctx_t        *c  = NULL;
    ngx_media_live_session_core_conf_t* conf= NULL;
    ngx_str_t                           key;
    ngx_uint_t                          len;
    u_char                             *last= NULL;
    struct shmcache_key_info shm_key;
    struct shmcache_value_info value;

    len = sizeof(ngx_media_live_session_ctx_t);

    conf = (ngx_media_live_session_core_conf_t *)
                ngx_get_conf(ngx_cycle->conf_ctx, ngx_media_live_session_module);
    if(NULL == conf) {
        return NGX_ERROR;
    }


    if(NGX_OK != ngx_media_live_cache_read(conf->cache,name,&c,&len)) {
       return NGX_ERROR;
    }

    shm_key.data = name->data;
    shm_key.len  = name->len;
    if( 0 != shmcache_get(&conf->cache,&shm_key,&value)) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "ngx_media_live_session_module: read fail.");
        return NGX_ERROR;
    }

    c = (ngx_media_live_session_ctx_t*)value->data;

    sc.status = c->status;
    sc.type   = c->type;
    sc.start  = c->start;
    sc.ttl    = c->ttl;
    sc.flux   = c->flux + flux;
    sc.last   = ngx_time();
    if( 0 != shmcache_delete(&conf->cache,&shm_key)) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "ngx_media_live_session_module: remove fail.");
        return NGX_ERROR;
    }
    if( 0 != shmcache_set(&conf->cache,&shm_key,(const u_char*)&sc,len,sc.ttl)) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "ngx_media_live_session_module: write fail.");
        return NGX_ERROR;
    }
    return NGX_OK;
}


