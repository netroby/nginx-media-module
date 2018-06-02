/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_task_module.c
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
#include "ngx_media_task_module.h"
#include "ngx_media_license_module.h"
#include "ngx_media_include.h"
#include "ngx_media_worker.h"
#include "libMediaKenerl.h"
#include "mk_def.h"


#define NGX_HTTP_TRANS_TASK_MAX     1000
#define NGX_HTTP_TRANS_WOEKER_MAX   32
#define NGX_HTTP_TRANS_PARAM_MAX    256
#define NGX_HTTP_STAT_HTML_ROW_MAX  4096
#define NGX_HTTP_TRANS_SENDRECV_MAX 5
#define NGX_HTTP_TRANS_REPORT_RESP_MAX 512

#define NGX_HTTP_STATIC_TASK_TIMER  60000

static const char* ERROR_MSG[] ={
    "success",
    "xml error",
    "param error",
    "the task is existed",
    "the task is not existed",
    "creat task fail",
    "the task run fail",
    "System error or unknow error"
};

typedef struct {
    ngx_array_t                    *task_args;
    ngx_flag_t                      task_monitor;
    ngx_str_t                       static_task;
    ngx_event_t                     static_task_timer;
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    ngx_resolver_t                 *resolver;
    ngx_msec_t                      resolver_timeout;
} ngx_media_main_conf_t;


ngx_media_task_ctx_t video_task_ctx;

/* http report contenxt */
typedef struct {
    ngx_buf_t                      *request;
    ngx_buf_t                      *response;
    ngx_peer_connection_t           peer;
    ngx_pool_t                     *pool;
    ngx_uint_t                      sendcount;
}ngx_http_trans_report_ctx_t;


/*************************video task init *****************************/
static ngx_int_t ngx_media_task_postconfiguration(ngx_conf_t *cf);
static char*     ngx_media_task_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void      ngx_media_task_static_init_timer(ngx_media_main_conf_t* conf);
static ngx_int_t ngx_media_task_init_process(ngx_cycle_t *cycle);
static void      ngx_media_task_exit_process(ngx_cycle_t *cycle);
static void*     ngx_media_task_create_main_conf(ngx_conf_t *cf);

/*************************video task request *****************************/
static void      ngx_media_task_recv_request(ngx_http_request_t *r);
static void      ngx_media_task_deal_xml_req(ngx_http_request_t *r,const char* req_xml,ngx_chain_t* out);
static void      ngx_media_task_deal_params(xmlNodePtr curNode,ngx_media_task_t* task,ngx_media_worker_ctx_t* worker);
static void      ngx_media_task_deal_worker_vari_params(ngx_media_task_t* task,ngx_media_worker_ctx_t* worker);
static void      ngx_media_task_deal_task_vari_params(ngx_media_task_t* task);
static ngx_int_t ngx_media_task_deal_init_workers(ngx_media_task_t* task);
static void      ngx_media_task_deal_triggers(xmlNodePtr curNode,ngx_str_t* taskid,ngx_media_worker_ctx_t* worker);
static ngx_int_t ngx_media_task_deal_workers(xmlNodePtr curNode,ngx_media_task_t* task);
static void      ngx_media_task_destory_workers(ngx_media_task_t* task);
static int       ngx_media_task_start_task(ngx_media_main_conf_t *conf,xmlDocPtr doc);
static int       ngx_media_task_stop_task(ngx_str_t* taskid);
static int       ngx_media_task_update_task(xmlDocPtr doc);
static void      ngx_media_task_check_task(ngx_event_t *ev);
static ngx_int_t ngx_media_task_check_task_exist(u_char* taskID);
static void      ngx_media_task_worker_watcher(ngx_uint_t status,ngx_int_t err_code,ngx_media_worker_ctx_t* ctx);
static void      ngx_media_task_dump_info(ngx_media_task_t* task);
static void      ngx_media_task_check_static_task(ngx_event_t *ev);
static ngx_int_t ngx_media_task_deal_static_xml(ngx_tree_ctx_t *ctx, ngx_str_t *path);







static ngx_command_t  ngx_media_task_commands[] = {

    { ngx_string(NGX_HTTP_TRANS_TASK),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_media_task_init,
      0,
      0,
      NULL },

    { ngx_string(NGX_HTTP_TASK_MONITOR),
       NGX_HTTP_MAIN_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_main_conf_t, task_monitor),
      NULL },

    { ngx_string(NGX_HTTP_TASK_ARGS),
       NGX_HTTP_MAIN_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_keyval_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_main_conf_t, task_args),
      NULL },

    { ngx_string(NGX_HTTP_STATIC_TASK),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_main_conf_t, static_task),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_media_task_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_media_task_postconfiguration,       /* postconfiguration */
    ngx_media_task_create_main_conf,        /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    NULL,                                   /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_task_module = {
    NGX_MODULE_V1,
    &ngx_media_task_module_ctx,            /* module context */
    ngx_media_task_commands,               /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_media_task_init_process,           /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_media_task_exit_process,           /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_media_task_handler(ngx_http_request_t *r)
{
    ngx_int_t    rc;
    ngx_uint_t count = ngx_media_task_get_cur_count();
    ngx_uint_t licesen_task =ngx_media_license_task_count();

    if (count >= licesen_task)
    {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "trans task is full!");
        return NGX_DECLINED;
    }

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD|NGX_HTTP_POST))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    /* deal the http request with xml content */
    r->request_body_in_single_buf = 1;
    rc = ngx_http_read_client_request_body(r,ngx_media_task_recv_request);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }
    /* return the NGX_DONE and send response by the ngx_media_task_recv_request */
    return NGX_DONE;
}

static ngx_int_t
ngx_media_task_postconfiguration(ngx_conf_t *cf)
{
    ngx_uint_t                   s;
    ngx_media_main_conf_t       *mmcf;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_http_core_main_conf_t   *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    cscfp = cmcf->servers.elts;

    for (s = 0; s < cmcf->servers.nelts; s++) {

        mmcf = cscfp[s]->ctx->srv_conf[ngx_media_task_module.ctx_index];

        clcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index];

        if((NULL == mmcf)||(NULL == clcf))
        {
            continue;
        }

        mmcf->resolver         = clcf->resolver;
        mmcf->resolver_timeout = clcf->resolver_timeout;
    }

    return NGX_OK;
}

static void *
ngx_media_task_create_main_conf(ngx_conf_t *cf)
{
    ngx_media_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->task_args = ngx_array_create(cf->pool, TASK_ARGS_MAX,
                                                sizeof(ngx_keyval_t));
    if (conf->task_args == NULL) {
        return NULL;
    }
    conf->task_monitor  = NGX_CONF_UNSET;
    ngx_str_null(&conf->static_task);
    conf->log           = cf->log;
    conf->pool          = cf->pool;
    conf->resolver      = NULL;
    conf->resolver_timeout = NGX_CONF_UNSET_MSEC;

    return conf;
}

static char*
ngx_media_task_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_task_handler;


    return NGX_CONF_OK;
}

static void
ngx_media_task_static_init_timer(ngx_media_main_conf_t* conf)
{
    ngx_memzero(&conf->static_task_timer, sizeof(ngx_event_t));
    conf->static_task_timer.handler = ngx_media_task_check_static_task;
    conf->static_task_timer.log     = conf->log;
    conf->static_task_timer.data    = conf;

    ngx_add_timer(&conf->static_task_timer,NGX_HTTP_STATIC_TASK_TIMER);
}


static ngx_int_t
ngx_media_task_init_process(ngx_cycle_t *cycle)
{
    ngx_media_main_conf_t *mainconf   = NULL;
    int task_monitor = 0;

    video_task_ctx.log = cycle->log;
    video_task_ctx.task_head  = NULL;
    video_task_ctx.task_count = 0;
    if (ngx_thread_mutex_create(&video_task_ctx.task_thread_mtx, cycle->log) != NGX_OK) {
        return NGX_ERROR;
    }

    mainconf = (ngx_media_main_conf_t *)ngx_http_cycle_get_module_main_conf(cycle, ngx_media_task_module);
    if(NULL == mainconf) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_task_module: Fail to main configuration");
        ngx_log_stderr(0, "ngx_media_task_module: Fail to main configuration");
        return NGX_ERROR;
    }

    if(NGX_CONF_UNSET != mainconf->task_monitor) {
        task_monitor = mainconf->task_monitor;
    }

    /* init the media kernel libary */
    int ret = mk_lib_init(NULL,task_monitor);
    if(MK_ERROR_CODE_OK != ret) {
        return NGX_ERROR;
    }

    /*init the all worker */
    ngx_media_register_all_worker();

    /* execs are always started by the first worker */
    if (ngx_process_slot) {
        return NGX_OK;
    }



    ngx_media_task_static_init_timer(mainconf);

    return NGX_OK;
}

static void
ngx_media_task_exit_process(ngx_cycle_t *cycle)
{

    return ;
}

static void
ngx_media_task_copy_global_args(ngx_media_main_conf_t *conf,ngx_media_task_t *task)
{
    ngx_keyval_t               *kv      = NULL;
    ngx_keyval_t               *task_kv = NULL;
    ngx_uint_t                  i       = 0;
    u_char                     *last    = NULL;


    kv = (ngx_keyval_t*)conf->task_args->elts;
    for(i = 0;i < conf->task_args->nelts;i++) {

        task_kv = ngx_list_push(task->arglist);
        if(NULL == task_kv) {
            return;
        }

        task_kv->key.data = ngx_pcalloc(task->pool, kv[i].key.len + 1);
        task_kv->key.len  = kv[i].key.len;
        last = ngx_cpymem(task_kv->key.data,kv[i].key.data,kv[i].key.len);
        *last = '\0';

        task_kv->value.data= ngx_pcalloc(task->pool, kv[i].value.len + 1);
        task_kv->value.len  = kv[i].value.len;
        last = ngx_cpymem(task_kv->value.data,kv[i].value.data,kv[i].value.len);
        *last = '\0';
        ngx_log_error(NGX_LOG_DEBUG, task->log,0, "copy the global arg,key:[%V] value:[%V]",
                                         &kv[i].key,&kv[i].value);
    }
    return;
}

static void
ngx_media_task_push_args(ngx_media_task_t *task,u_char* key,u_char* value)
{
    ngx_keyval_t               *kv      = NULL;
    u_char                     *last    = NULL;
    ngx_uint_t                  size    = 0;

    kv = ngx_list_push(task->arglist);
    if(NULL == kv) {
        return;
    }

    size = ngx_strlen(key);
    kv->key.data = ngx_pcalloc(task->pool, size+ 1);
    kv->key.len  = size;
    last = ngx_cpymem(kv->key.data,key,size);
    *last = '\0';

    size = ngx_strlen(value);
    kv->value.data= ngx_pcalloc(task->pool, size + 1);
    kv->value.len  = size;
    last = ngx_cpymem(kv->value.data,value,size);
    *last = '\0';

    ngx_log_error(NGX_LOG_DEBUG, task->log,0, "push the arg,key:[%V] value:[%V]",
                                         &kv->key,&kv->value);
}


static ngx_media_task_t*
ngx_media_task_create_task(ngx_media_main_conf_t *conf)
{
    ngx_media_task_t              *task = NULL;
    ngx_pool_t                    *pool = NULL;
    ngx_uint_t                     lens = 0;


    pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, video_task_ctx.log);
    if (pool == NULL) {
        return NULL;
    }

    lens = sizeof(ngx_media_task_t);

    task = (ngx_media_task_t*)ngx_pcalloc(pool,lens);
    if (task == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    ngx_memzero(task, lens);
    task->pool     = pool;
    task->log      = video_task_ctx.log;
    task->resolver = conf->resolver;
    task->resolver_timeout = conf->resolver_timeout;

    if (ngx_thread_mutex_create(&task->task_mtx, task->log) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, video_task_ctx.log, 0, "create task,create the mutex fail!");
        ngx_destroy_pool(pool);
        return NULL;
    }
    task->arglist     = ngx_list_create(task->pool,TASK_ARGS_MAX,sizeof(ngx_keyval_t));
    task->rep_inter   = 60;
    task->prev_task   = NULL;
    task->next_task   = NULL;
    ngx_str_null(&task->xml);
    task->status      = ngx_media_worker_status_init;
    task->error_code  = NGX_MEDIA_ERROR_CODE_OK;
    task->workers     = ngx_list_create(task->pool,NGX_HTTP_TRANS_WOEKER_MAX,sizeof(ngx_media_worker_ctx_t));

    ngx_memzero(&task->time_event, sizeof(ngx_event_t));
    task->time_event.handler = ngx_media_task_check_task;
    task->time_event.log = video_task_ctx.log;
    task->time_event.data = task;

    ngx_media_task_copy_global_args(conf,task);

    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    if(NULL != video_task_ctx.task_head) {
       video_task_ctx.task_head->prev_task = task;
       task->next_task = video_task_ctx.task_head;
    }
    video_task_ctx.task_head = task;
    video_task_ctx.task_count++;

    ngx_add_timer(&task->time_event, TASK_CHECK_INTERVAL);

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, task->log, 0,
                          "ngx http video task create the task current count:%d .",video_task_ctx.task_count);

    if (ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return NULL;
    }
    return task;
}
static void
ngx_media_task_destory_task(ngx_media_task_t* task)
{
    if(NULL == task) {
        return;
    }

    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return ;
    }
    ngx_media_task_t* tmptask = (ngx_media_task_t*)video_task_ctx.task_head;
    while(NULL != tmptask) {

        if(tmptask == task) {

            if(NULL != tmptask->next_task) {
                ngx_media_task_t* next = tmptask->next_task;
                next->prev_task = tmptask->prev_task;
            }

            if(NULL != tmptask->prev_task) {
                ngx_media_task_t* prev = tmptask->prev_task;
                prev->next_task = tmptask->next_task;
            }
            else {
                /* first node */
                video_task_ctx.task_head = tmptask->next_task;
            }

            tmptask->next_task = NULL;
            tmptask->prev_task = NULL;

            ngx_media_task_destory_workers(task);
            (void)ngx_thread_mutex_destroy(&task->task_mtx,task->log);

            ngx_destroy_pool(task->pool);
            video_task_ctx.task_count--;

            break;
        }
        tmptask = tmptask->next_task;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, task->log, 0,
                          "ngx http video task destory the task current count:%d .",video_task_ctx.task_count);

    if (ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return ;
    }

    return;
}

static ngx_int_t
ngx_media_task_check_task_exist(u_char* taskID)
{
    if(NULL == taskID) {
        return NGX_OK;
    }

    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return NGX_ERROR;
    }
    ngx_media_task_t* tmptask = (ngx_media_task_t*)video_task_ctx.task_head;
    while(NULL != tmptask) {
        if(ngx_strncmp(tmptask->task_id.data,taskID,ngx_strlen(taskID)) == 0) {
            if (ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
                return NGX_OK;
            }
            return NGX_OK;
        }
        tmptask = tmptask->next_task;
    }

    if (ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_ERROR;
}

static void
ngx_media_task_destory_workers(ngx_media_task_t* task)
{
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t               *part   = NULL;

    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        return ;
    }

    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            if(ngx_thread_mutex_lock(&worker->work_mtx, worker->log) == NGX_OK) {
                if(NULL != worker->worker) {
                    if(NGX_OK != worker->worker->release_worker(worker)) {
                        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, worker->log, 0,
                                     "ngx http video task release task worker fail.");
                    }
                }
                ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
            }
        }
        part = part->next;
    }
    if (ngx_thread_mutex_unlock(&task->task_mtx, task->log) != NGX_OK) {
        return ;
    }
}
static void
ngx_media_task_write_dummy_handler(ngx_event_t *ev)
{
    ngx_log_debug0(NGX_LOG_DEBUG, ev->log, 0,
                   "trans rreport http dummy handler");
}

static void
ngx_media_task_write_handler(ngx_event_t *wev)
{
    ssize_t                      n, size;
    ngx_connection_t             *c;
    ngx_http_trans_report_ctx_t  *ctx;

    c = wev->data;
    ctx  = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG, wev->log, 0,
                   "trans report http write handler");

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_ERR, wev->log, NGX_ETIMEDOUT,
                      "trans report http server timed out");

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
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "video task send to peer fail!");
        return;
    }

    if (n > 0) {
        ctx->request->pos += n;

        if (n == size) {
            wev->handler = ngx_media_task_write_dummy_handler;

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

    if (NGX_HTTP_TRANS_SENDRECV_MAX < ctx->sendcount) {
        ngx_close_connection(c);
        ngx_destroy_pool(ctx->pool);
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "video task send to peer try max times!");
        return;
    }

    if (!wev->timer_set) {
        ngx_add_timer(wev, 1000);
    }
}

static void
ngx_media_task_read_handler(ngx_event_t *rev)
{
    ssize_t                      n, size;
    ngx_connection_t             *c;
    ngx_http_trans_report_ctx_t  *ctx;

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
ngx_media_task_report_create_request(ngx_media_task_t *task,
                                         ngx_http_trans_report_ctx_t *ctx)
{
    ngx_media_worker_ctx_t   *worker      = NULL;
    ngx_media_worker_ctx_t   *array       = NULL;
    ngx_list_part_t          *part        = NULL;
    xmlDocPtr                 doc         = NULL;/* document pointer */
    xmlNodePtr                report_node = NULL;
    xmlNodePtr                task_node   = NULL;
    xmlNodePtr                result_node = NULL;
    xmlNodePtr                works_node  = NULL;
    xmlNodePtr                work_node   = NULL;
    xmlChar                  *xmlbuff     = NULL;
    int                       buffersize  = 0;
    u_char                   *last        = NULL;

    size_t     len;
    ngx_buf_t  *b;
    u_char buf[128];
    ngx_memzero(&buf, 128);

    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        return NULL;
    }

    /* Creates a new document, a node and set it as a root node*/
    doc = xmlNewDoc(BAD_CAST "1.0");
    report_node = xmlNewNode(NULL, BAD_CAST "report");
    xmlNewProp(report_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlDocSetRootElement(doc, report_node);
    task_node   = xmlNewNode(NULL, BAD_CAST "task");
    xmlNewProp(task_node, BAD_CAST "taskid", BAD_CAST task->task_id.data);

    if(task->status == ngx_media_worker_status_start) {
        xmlNewProp(task_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_START);
    }
    else if(task->status == ngx_media_worker_status_running) {
        xmlNewProp(task_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_RUNNING);
    }
    else if(task->status == ngx_media_worker_status_end) {
        xmlNewProp(task_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_STOP);
    }
    else if(task->status == ngx_media_worker_status_break) {
        xmlNewProp(task_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_BREAK);
    }
    else {
        xmlNewProp(task_node, BAD_CAST TASK_STATUS, BAD_CAST "invalid");
    }

    result_node = xmlNewNode(NULL, BAD_CAST "result");
    ngx_memzero(&buf, 128);
    last = ngx_snprintf(buf, 128, "%d", task->error_code);
    *last = '\0';
    xmlNewProp(result_node, BAD_CAST "errorcode", BAD_CAST buf);
    xmlNewProp(result_node, BAD_CAST "describe", BAD_CAST ERROR_MSG[task->error_code]);
    xmlAddChild(task_node, result_node);

    works_node = xmlNewNode(NULL, BAD_CAST "workers");
    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            if(ngx_thread_mutex_lock(&worker->work_mtx, worker->log) == NGX_OK) {

                work_node = xmlNewNode(NULL, BAD_CAST "worker");

                xmlNewProp(work_node, BAD_CAST TASK_XML_WORKER_ID, BAD_CAST worker->wokerid.data);

                ngx_memzero(&buf, 128);
                last = ngx_snprintf(buf, 128, "%d", worker->type);
                *last = '\0';
                xmlNewProp(work_node, BAD_CAST TASK_XML_WORKER_TYPE, BAD_CAST buf);

                ngx_memzero(&buf, 128);
                last = ngx_snprintf(buf, 128, "%d", worker->master);
                *last = '\0';
                xmlNewProp(work_node, BAD_CAST TASK_XML_WORKER_MASTER, BAD_CAST buf);

                if(worker->status == ngx_media_worker_status_init) {
                    xmlNewProp(work_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_INIT);
                }
                else if(worker->status == ngx_media_worker_status_start) {
                    xmlNewProp(work_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_START);
                }
                else if(worker->status == ngx_media_worker_status_running) {
                    xmlNewProp(work_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_RUNNING);
                }
                else if(worker->status == ngx_media_worker_status_end) {
                    xmlNewProp(work_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_STOP);
                }
                else if(worker->status == ngx_media_worker_status_break) {
                    xmlNewProp(work_node, BAD_CAST TASK_STATUS, BAD_CAST TASK_COMMAND_BREAK);
                }
                else {
                    xmlNewProp(work_node, BAD_CAST TASK_STATUS, BAD_CAST "invalid");
                }
                xmlAddChild(works_node,work_node);
                ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
            }
        }
        part = part->next;
    }

    xmlAddChild(task_node, works_node);

    xmlAddChild(report_node, task_node);



    xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
    ngx_memzero(&buf, 128);
    last = ngx_snprintf(buf, 128, "%d", buffersize);
    *last = '\0';


    len = sizeof("POST ") - 1 + task->url.uri.len + sizeof(" HTTP/1.1" CRLF) - 1
          + sizeof("Host: ") - 1 + task->url.host.len + sizeof(CRLF) - 1
          + sizeof("User-Agent: AllMedia") - 1 + sizeof(CRLF) - 1
          + sizeof("Connection: close") - 1 + sizeof(CRLF) - 1
          + sizeof("Content-Type: application/xml") - 1 + sizeof(CRLF) - 1
          + sizeof("Content-Length: ") - 1 + ngx_strlen(buf) + sizeof(CRLF) - 1
          + sizeof(CRLF) - 1
          + buffersize;

    b = ngx_create_temp_buf(ctx->pool, len);
    if (b == NULL) {
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create temp buf fail!");
        if (ngx_thread_mutex_unlock(&task->task_mtx, task->log) != NGX_OK) {
            return NULL;
        }
        return NULL;
    }

    b->last = ngx_cpymem(b->last, "POST ", sizeof("POST ") - 1);
    b->last = ngx_copy(b->last, task->url.uri.data, task->url.uri.len);
    b->last = ngx_cpymem(b->last, " HTTP/1.1" CRLF,
                         sizeof(" HTTP/1.1" CRLF) - 1);

    b->last = ngx_cpymem(b->last, "Host: ", sizeof("Host: ") - 1);
    b->last = ngx_copy(b->last, task->url.host.data,
                         task->url.host.len);
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
    if (ngx_thread_mutex_unlock(&task->task_mtx, task->log) != NGX_OK) {
        return NULL;
    }

    return b;
}

static void
ngx_media_task_report_progress(ngx_media_task_t *task)
{
    ngx_int_t                      rc;
    ngx_http_trans_report_ctx_t   *ctx;
    ngx_pool_t                    *pool = NULL;

    if(0 == task->url.url.len)
    {
        return;
    }

    if(NULL == task->url.addrs)
    {
        return;
    }


    pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, task->log);
    if (pool == NULL) {
        return;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_http_trans_report_ctx_t));
    if (ctx == NULL) {
        ngx_destroy_pool(pool);

        return;
    }

    ctx->pool      = pool;
    ctx->sendcount = 0;

    ctx->request = ngx_media_task_report_create_request(task,ctx);
    if (ctx->request == NULL) {
        ngx_destroy_pool(pool);

        return;
    }

    ctx->peer.sockaddr = task->url.addrs->sockaddr;
    ctx->peer.socklen = task->url.addrs->socklen;
    ctx->peer.name = &task->url.addrs->name;
    ctx->peer.get = ngx_event_get_peer;
    ctx->peer.log = task->log;
    ctx->peer.log_error = NGX_ERROR_ERR;

    rc = ngx_event_connect_peer(&ctx->peer);

    if (rc != NGX_OK && rc != NGX_AGAIN ) {
        if (ctx->peer.connection) {
            ngx_close_connection(ctx->peer.connection);
        }
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task connect to peer fail!");
        ngx_destroy_pool(pool);
        return;
    }

    ctx->peer.connection->data = ctx;
    ctx->peer.connection->pool = ctx->pool;

    ctx->peer.connection->read->handler = ngx_media_task_read_handler;
    //ctx->peer.connection->read->data = ctx;
    ctx->peer.connection->write->handler = ngx_media_task_write_handler;
    //ctx->peer.connection->write->data = ctx;

    //ngx_msleep(100);


    ngx_add_timer(ctx->peer.connection->read, 5000);
    ngx_add_timer(ctx->peer.connection->write, 1000);

    /* send the request direct */
    if (rc == NGX_OK ) {
        ngx_media_task_write_handler(ctx->peer.connection->write);
    }
    return;
}

static void
ngx_media_task_parse_report_url(ngx_media_task_t *task)
{
    ngx_memzero(&task->url, sizeof(ngx_url_t));

    task->url.url = task->rep_url;
    task->url.no_resolve = 1;
    task->url.uri_part = 1;

    if (ngx_strncmp(task->url.url.data, "http://", 7) == 0) {
        task->url.url.len -= 7;
        task->url.url.data += 7;
    }

    if (ngx_parse_url(task->pool, &task->url) != NGX_OK) {
         if (task->url.err) {
            ngx_log_error(NGX_LOG_ERR, task->log, 0,
                          "%s in task \"%V\"", task->url.err, &task->url.url);
        }
        return ;
    }
    return;
}


static void
ngx_media_task_deal_params(xmlNodePtr curNode,ngx_media_task_t* task,ngx_media_worker_ctx_t* worker)
{
    xmlNodePtr paramNode  = NULL;
    xmlChar*   name       = NULL;
    xmlChar*   value      = NULL;
    u_char*    param      = NULL;
    u_char*    last       = NULL;

    paramNode = curNode->children;
    while(NULL != paramNode) {
        if (xmlStrcmp(paramNode->name, BAD_CAST TASK_XML_PARAM)) {
            paramNode = paramNode->next;
            continue;
        }
        name = xmlGetProp(paramNode,BAD_CAST COMMON_XML_NAME);
        if(NULL != name) {
            size_t lens = ngx_strlen(name);
            param = ngx_pcalloc(worker->pool,lens+2);
            param[0] = '-';
            last =ngx_copy(&param[1],(u_char*)name,lens);
            worker->paramlist[worker->nparamcount] = param;
            worker->nparamcount++;

            value = xmlGetProp(paramNode,BAD_CAST COMMON_XML_VALUE);
            if(NULL != value) {
                lens  = ngx_strlen(value);
                param = ngx_pcalloc(worker->pool,lens+1);
                last =ngx_copy(param,(u_char*)value,lens);
                *last = '\0';
                worker->paramlist[worker->nparamcount] = param;
                worker->nparamcount++;
                ngx_media_task_push_args(task, (u_char*)name ,(u_char*)value);
                xmlFree(value);
            }
            xmlFree(name);
        }
        paramNode = paramNode->next;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, task->log, 0,
                          "ngx http video task deal params end.");
    return ;
}

static ngx_str_t*
ngx_media_task_deal_find_variable_param(ngx_media_task_t* task,u_char* name)
{
    ngx_keyval_t                  *kv      = NULL;
    ngx_keyval_t                  *array   = NULL;
    ngx_list_part_t               *part    = NULL;
    ngx_str_t                     *str     = NULL;
    ngx_uint_t                     size    = 0;
    ngx_uint_t                     cmpsize = 0;

    size = ngx_strlen(name);

    part  = &(task->arglist->part);
    while (part)
    {
        array = (ngx_keyval_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            kv = &array[loop];
            cmpsize = ngx_strlen(kv->key.data);
            if(size > cmpsize) {
                 cmpsize = size;
            }

            ngx_log_error(NGX_LOG_DEBUG, task->log,0, "compare the arg,name:[%s],key:[%V] value:[%V]",
                                         name,&kv->key,&kv->value);


            if( 0== ngx_strncmp(name, kv->key.data, cmpsize)) {
                str = &kv->value;
                return str;
            }
        }
        part = part->next;
    }

    return NULL;
}

static void
ngx_media_task_deal_worker_vari_params(ngx_media_task_t* task,ngx_media_worker_ctx_t* worker)
{
    ngx_int_t      i     = 0;
    ngx_str_t     *arg   = NULL;
    u_char        *value = NULL;
    u_char        *vari  = NULL;
    u_char        *end   = NULL;
    u_char        *last  = NULL;
    u_char         str[TRANS_STRING_MAX_LEN];
    ngx_uint_t     strLen= 0;

    ngx_memzero(&str,TRANS_STRING_MAX_LEN);

    for(i = 0 ; i < worker->nparamcount; i++) {
        value = worker->paramlist[i];

        if('-' == value[0]) {
            continue;
        }
        strLen = ngx_strlen(value);
        vari = (u_char *)ngx_strstr(value, "$(");
        while (NULL != vari) {
            end = (u_char*)ngx_strchr(value,')');
            if (NULL == end) {
                ngx_log_error(NGX_LOG_DEBUG, task->log,0, "can't find the arg end tag')',the arg str:[%s]",vari);
                break;
            }

            ngx_uint_t size = end - vari - 2;

            if(0 == size) {
                ngx_log_error(NGX_LOG_DEBUG, task->log,0, "the arg lens is zore,value:[%s]",value);
                break;
            }


           last  = ngx_copy(str,(vari+2),size);
           *last = '\0';

           ngx_log_error(NGX_LOG_DEBUG, task->log,0, "find arg:[%s] to covert the real value",str);

            arg = ngx_media_task_deal_find_variable_param(task,&str[0]);

            if(NULL == arg) {
                ngx_log_error(NGX_LOG_WARN, task->log,0, " can't covert arg:[%s] to  the real value",str);
                break;
            }

            ngx_uint_t len = arg->len + strLen;
            u_char* data = ngx_pcalloc(task->pool,len);

            if(NULL == data) {
                ngx_log_error(NGX_LOG_WARN, task->log,0, "allocate memory for real arg fail.");
                break;
            }

            ngx_uint_t preLen = vari - value;
            ngx_uint_t sufLen = strLen - (end - value +1);
            u_char* buf = data;

            if(0 < preLen ) {
                last  = ngx_snprintf(buf,preLen,"%s",value);
                *last = '\0';
                buf += preLen;
                ngx_log_error(NGX_LOG_DEBUG, task->log,0, "add the prefix:[%s],value:[%s]",value,data);
            }

            strLen = ngx_strlen(arg->data);
            last  = ngx_snprintf(buf,strLen,"%V",arg);
            *last = '\0';
            buf += strLen;

            ngx_log_error(NGX_LOG_DEBUG, task->log,0, "add the real value:[%V],value:[%s]",arg,data);
            if(0 < sufLen ) {
                u_char* pEnd = end+1;
                last  = ngx_snprintf(buf,sufLen,"%s",pEnd);
                *last = '\0';
                buf += sufLen;
                ngx_log_error(NGX_LOG_DEBUG, task->log,0, "add the suffix:[%s],value:[%s]",pEnd,data);
            }
            *buf ='\0';

            /*
            ngx_log_error(NGX_LOG_DEBUG, task->log,0, "prefix length:[%d] value length:[%d] suffix length:[%d].",
                                                      preLen,strLen,sufLen);
            */
            ngx_log_error(NGX_LOG_DEBUG, task->log,0, "replace the param:[%s] to new value:[%s]",worker->paramlist[i],data);
            worker->paramlist[i] = data;

            value = worker->paramlist[i];
            strLen = ngx_strlen(value);
            vari = (u_char *)ngx_strstr(value, "$(");
        }
    }
}

static void
ngx_media_task_deal_task_vari_params(ngx_media_task_t* task)
{
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t               *part   = NULL;

    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            /* deal the variable params */
            ngx_media_task_deal_worker_vari_params(task,worker);
        }
        part = part->next;
    }

}

static ngx_int_t
ngx_media_task_deal_init_workers(ngx_media_task_t* task)
{
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t               *part   = NULL;

    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            /* init the worker function */
            if(NGX_OK != ngx_media_worker_init_worker_ctx(worker,ngx_media_task_worker_watcher)) {
                worker->worker = NULL;
                worker->status = ngx_media_worker_status_break;
                ngx_log_error(NGX_LOG_EMERG, task->log, 0, "ini the worker ctx fail!");
                return NGX_ERROR;
            }
        }
        part = part->next;
    }
    return NGX_OK;
}


static void
ngx_media_task_deal_triggers(xmlNodePtr curNode,ngx_str_t* taskid,ngx_media_worker_ctx_t* worker)
{
    xmlNodePtr                   triggerNode = NULL;
    xmlChar*                     attr_value  = NULL;
    u_char*                      last        = NULL;
    size_t                       lens        = 0;
    ngx_media_worker_trigger_ctx *trigger     = NULL;

    triggerNode = curNode->children;
    while(NULL != triggerNode) {
        if (xmlStrcmp(triggerNode->name, BAD_CAST TASK_XML_TRIGGER)) {
            triggerNode = triggerNode->next;
            continue;
        }
        /* after */
        attr_value = xmlGetProp(triggerNode,BAD_CAST TASK_XML_TRIGGER_AFTER);
        if(NULL == attr_value) {
            continue;
        }

        if(0 == ngx_strncmp(attr_value,TASK_COMMAND_START,ngx_strlen(attr_value))) {
            trigger = ngx_list_push(worker->triggerStart);
        }
        else if (0 == ngx_strncmp(attr_value,TASK_COMMAND_STOP,ngx_strlen(attr_value))) {
            trigger = ngx_list_push(worker->triggerEnd);
        }
        else {
            xmlFree(attr_value);
            continue;
        }
        xmlFree(attr_value);

        if(NULL == trigger) {
            return;
        }

        ngx_str_null(&trigger->wokerid);
        trigger->delay  = -1;
        trigger->taskid.data =  ngx_pcalloc(worker->pool,taskid->len+1);
        last = ngx_copy(trigger->taskid.data,taskid->data,taskid->len);
        *last = '\0';
        trigger->taskid.len  = taskid->len;

        /* worker */
        attr_value = xmlGetProp(triggerNode,BAD_CAST TASK_XML_TRIGGER_WOKER);
        if(NULL != attr_value) {
            lens = ngx_strlen(attr_value);
            trigger->wokerid.data = ngx_pcalloc(worker->pool,lens+1);
            trigger->wokerid.len  = lens;
            last =ngx_copy(trigger->wokerid.data,(u_char*)attr_value,lens);
            *last = '\0';
            xmlFree(attr_value);
        }
        /* delay */
        attr_value = xmlGetProp(triggerNode,BAD_CAST TASK_XML_TRIGGER_DELAY);
        if(NULL != attr_value) {
            trigger->delay =  ngx_atoi(attr_value, ngx_strlen(attr_value));
            xmlFree(attr_value);
        }

        triggerNode = triggerNode->next;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, worker->log, 0,
                          "ngx http video task deal triggers end.");
    return ;
}

static void
ngx_media_task_error_xml(ngx_http_request_t *r,ngx_chain_t* out,
                             ngx_int_t code,xmlNodePtr curNode)
{
    xmlDocPtr resp_doc    = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, node = NULL;/* node pointers */
    u_char*  pbuff        = NULL;
    u_char* last          = NULL;
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;


    ngx_int_t  errcode = code;
    u_char   errorbuf[TRANS_STRING_MAX_LEN];
    ngx_memzero(&errorbuf, TRANS_STRING_MAX_LEN);

    if((0>errcode)||(NGX_MEDIA_ERROR_CODE_SYS_ERROR < errcode)) {
        errcode = NGX_MEDIA_ERROR_CODE_SYS_ERROR;
    }

    last  = ngx_snprintf(errorbuf, TRANS_STRING_MAX_LEN,"%d", errcode);
    *last = '\0';

    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST "resp");
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST errorbuf);
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST ERROR_MSG[errcode]);

    xmlDocSetRootElement(resp_doc, root_node);

    if(NULL != curNode) {
        node = xmlCopyNodeList(curNode);
        xmlAddChild(root_node, node);
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return ;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return ;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);
    return;
}

static ngx_int_t
ngx_media_task_deal_workers(xmlNodePtr curNode,ngx_media_task_t* task)
{
    xmlNodePtr                   paramSNode = NULL;
    xmlNodePtr                   workerNode = NULL;
    xmlChar*                     attr_value = NULL;
    ngx_media_worker_ctx_t      *pWorker    = NULL;
    u_char                      *last       = NULL;

    workerNode  = curNode->children;
    while(NULL != workerNode) {
        if (xmlStrcmp(workerNode->name, BAD_CAST TASK_XML_WORKER)) {
            workerNode = workerNode->next;
            continue;
        }
        pWorker = (ngx_media_worker_ctx_t*)ngx_list_push(task->workers);
        if(NULL == pWorker){
            ngx_log_error(NGX_LOG_EMERG,task->log, 0,
                          "ngx http video task create the worker fail.");
            return NGX_ERROR;
        }

        pWorker->pool              = task->pool;
        pWorker->log               = task->log;
        pWorker->resolver          = task->resolver;
        pWorker->resolver_timeout  = task->resolver_timeout;

        ngx_str_null(&pWorker->wokerid);
        pWorker->type   = ngx_media_worker_invalid;
        pWorker->master = 1;
        pWorker->starttime = 0;
        pWorker->updatetime = 0;
        pWorker->error_code = NGX_MEDIA_ERROR_CODE_OK;
        pWorker->status = ngx_media_worker_status_init;
        pWorker->nparamcount = 0;
        ngx_uint_t lens = NGX_HTTP_TRANS_PARAM_MAX*sizeof(u_char*);
        pWorker->paramlist = ngx_pcalloc(pWorker->pool,lens);
        if (pWorker->paramlist == NULL) {
            return NGX_ERROR;
        }
        ngx_memzero(pWorker->paramlist, lens);

        if (ngx_thread_mutex_create(&pWorker->work_mtx, task->log) != NGX_OK) {
            ngx_log_error(NGX_LOG_EMERG, task->log, 0, "create worker,create the mutex fail!");
            return NGX_ERROR;
        }
        pWorker->worker = NULL;
        pWorker->priv_data_size = 0;
        pWorker->priv_data    = NULL;;
        pWorker->triggerStart =  ngx_list_create(pWorker->pool,NGX_HTTP_WORKER_TRIGGER_MAX,sizeof(ngx_media_worker_trigger_ctx));
        pWorker->triggerEnd   =  ngx_list_create(pWorker->pool,NGX_HTTP_WORKER_TRIGGER_MAX,sizeof(ngx_media_worker_trigger_ctx));

        attr_value = xmlGetProp(workerNode,BAD_CAST TASK_XML_WORKER_ID);
        if(NULL != attr_value) {
            pWorker->wokerid.data = ngx_pcalloc(task->pool,ngx_strlen(attr_value)+1);
            pWorker->wokerid.len  = ngx_strlen(attr_value);
            last = ngx_copy(pWorker->wokerid.data, attr_value, ngx_strlen(attr_value));
            *last = '\0';
        }

        attr_value = xmlGetProp(workerNode,BAD_CAST TASK_XML_WORKER_TYPE);
        if(NULL != attr_value) {
            pWorker->type = ngx_atoi(attr_value,ngx_strlen(attr_value));
        }
        attr_value = xmlGetProp(workerNode,BAD_CAST TASK_XML_WORKER_MASTER);
        if(NULL != attr_value) {
            pWorker->master = ngx_atoi(attr_value, ngx_strlen(attr_value));
        }
        paramSNode = workerNode->children;
        while(NULL != paramSNode) {
           if(!xmlStrcmp(paramSNode->name, BAD_CAST TASK_XML_PARAMS)) {
               /* deal the common params */
               ngx_media_task_deal_params(paramSNode,task,pWorker);
           }
           else if(!xmlStrcmp(paramSNode->name, BAD_CAST TASK_XML_TRIGGERS)) {
               /* deal the trigger params */
               ngx_media_task_deal_triggers(paramSNode,&task->task_id,pWorker);
           }
           /* unknow the params type */
           paramSNode = paramSNode->next;
        }
        workerNode = workerNode->next;
    }

    ngx_media_task_deal_task_vari_params(task);

    if(NGX_OK != ngx_media_task_deal_init_workers(task)) {
        return NGX_ERROR;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, task->log, 0,
                          "ngx http video task deal workers end.");
    return NGX_OK;
}

static int
ngx_media_task_start_task(ngx_media_main_conf_t *conf,xmlDocPtr doc)
{
    xmlNodePtr                   curNode    = NULL;
    xmlChar                     *attr_value = NULL;
    u_char                      *last       = NULL;
    ngx_media_task_t            *task       = NULL;
    ngx_media_worker_ctx_t      *worker     = NULL;
    ngx_media_worker_ctx_t      *array      = NULL;
    ngx_list_part_t             *part       = NULL;
    xmlChar                     *xmlbuff    = NULL;
    int                          buffersize = 0;
    ngx_int_t                    bStart     = 0;

    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode)
    {
       return NGX_MEDIA_ERROR_CODE_XML_ERROR;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST "req"))
    {
       return NGX_MEDIA_ERROR_CODE_XML_ERROR;
    }

    curNode = curNode->children;

    if (xmlStrcmp(curNode->name, BAD_CAST "task"))
    {
       return NGX_MEDIA_ERROR_CODE_XML_ERROR;
    }

    attr_value = xmlGetProp(curNode,BAD_CAST "taskid");
    if(NULL == attr_value){
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                                 "ngx http video task start task ,get task id fail.");
        return NGX_MEDIA_ERROR_CODE_XML_ERROR;
    }

    if(NGX_OK == ngx_media_task_check_task_exist((u_char*)attr_value)) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                                 "ngx http video task start task ,the  task id exist.");
        return NGX_MEDIA_ERROR_CODE_TASK_EXIST;
    }

    task = ngx_media_task_create_task(conf);
    if (task == NULL) {
        return NGX_MEDIA_ERROR_CODE_CREATE_TASK_FAIL;
    }

    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        ngx_media_task_destory_task(task);
        return NGX_MEDIA_ERROR_CODE_CREATE_TASK_FAIL;
    }

    attr_value = xmlGetProp(curNode,BAD_CAST "taskid");
    if(NULL != attr_value){
        task->task_id.data = ngx_pcalloc(task->pool,ngx_strlen(attr_value)+1);
        last = ngx_copy(task->task_id.data,attr_value,ngx_strlen(attr_value));
        task->task_id.len = ngx_strlen(attr_value);
        *last = '\0';
    }

    ngx_media_task_push_args(task,(u_char*)"taskid",attr_value);

    curNode = curNode->children;

    while(curNode != NULL)
    {
       if (!xmlStrcmp(curNode->name, BAD_CAST"report"))
       {
            attr_value = xmlGetProp(curNode,BAD_CAST "interval");
            if(NULL != attr_value){
                task->rep_inter = ngx_atoi(attr_value,ngx_strlen(attr_value));
            }
            attr_value = xmlGetProp(curNode,BAD_CAST "url");
            if(NULL != attr_value){
                task->rep_url.data = ngx_pcalloc(task->pool,ngx_strlen(attr_value)+1);
                last = ngx_copy(task->rep_url.data,attr_value,ngx_strlen(attr_value));
                task->rep_url.len = ngx_strlen(attr_value);
                *last = '\0';
                ngx_media_task_parse_report_url(task);
            }
       }
       else if (!xmlStrcmp(curNode->name, BAD_CAST "workers"))
       {
           if(NGX_OK != ngx_media_task_deal_workers(curNode,task)) {
                bStart = 0;
                goto error_start;
           }
       }
       curNode = curNode->next;
    }

    *last = '\0';

    /* save the xml doc */
    xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
    task->xml.data = ngx_pcalloc(task->pool,buffersize+1);
    task->xml.len  = buffersize;

    last = ngx_copy(task->xml.data,(u_char*)xmlbuff,buffersize);
    *last ='\0';

    /* dump the worker params */
    ngx_media_task_dump_info(task);

    /* start the master worker first */
    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            if((worker->master)&&(NULL != worker->worker)) {
                if(NGX_OK != worker->worker->start_worker(worker)) {
                    ngx_log_error(NGX_LOG_DEBUG, task->log, 0,"ngx http video start task worker fail.");
                    worker->status = ngx_media_worker_status_break;
                }
                bStart = 1;
            }
        }
        part = part->next;
    }

error_start:
    if(!bStart) {
        part  = &(task->workers->part);
        while (part)
        {
            array = (ngx_media_worker_ctx_t*)(part->elts);
            ngx_uint_t loop = 0;
            for (; loop < part->nelts; loop++)
            {
                worker = &array[loop];
                worker->status = ngx_media_worker_status_break;
            }
            part = part->next;
        }
        task->status = ngx_media_worker_status_break;/* the error task will be destory by the time checker */
        ngx_thread_mutex_unlock(&task->task_mtx, task->log);
        return NGX_MEDIA_ERROR_CODE_CREATE_TASK_FAIL;
    }
    task->status = ngx_media_worker_status_start;
    ngx_thread_mutex_unlock(&task->task_mtx, task->log);
    /* report the start status */
    ngx_media_task_report_progress(task);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task start task end.");
    return NGX_MEDIA_ERROR_CODE_OK;
}
static int
ngx_media_task_stop_task(ngx_str_t* taskid)
{
    ngx_media_task_t         *task   = NULL;
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t               *part   = NULL;
    ngx_uint_t                     stop   = 0;

    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return NGX_MEDIA_ERROR_CODE_SYS_ERROR;
    }

    task = video_task_ctx.task_head;

    while(NULL != task) {
        if(ngx_strncmp(taskid->data,task->task_id.data,task->task_id.len) == 0) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task find the task:[%V] and stop it.",&task->task_id);
            if (ngx_thread_mutex_lock(&task->task_mtx, task->log) == NGX_OK) {
                part  = &(task->workers->part);
                while (part)
                {
                    array = (ngx_media_worker_ctx_t*)(part->elts);
                    ngx_uint_t loop = 0;
                    for (; loop < part->nelts; loop++)
                    {
                        worker = &array[loop];
                        if(ngx_thread_mutex_lock(&worker->work_mtx, worker->log) == NGX_OK) {
                            if((ngx_media_worker_status_break == worker->status)
                                ||(ngx_media_worker_status_end == worker->status)){
                                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                                             "ngx http video task stop task worker,but the worker is stopped.");
                                continue;
                            }
                            if((NULL == worker->worker)) {
                                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                                             "ngx http video task stop task worker,but the worker is null.");
                                continue;
                            }
                            if(NGX_OK != worker->worker->stop_worker(worker)) {
                                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                                             "ngx http video task stop task worker fail.");
                            }
                            ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
                        }
                    }
                    part = part->next;
                }
                ngx_thread_mutex_unlock(&task->task_mtx, task->log);
                stop = 1;
            }
            break;
        }

        task = task->next_task;
    }

    if (ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return NGX_MEDIA_ERROR_CODE_SYS_ERROR;
    }

    if(!stop) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task stop task end,but there is no task.");
        return NGX_MEDIA_ERROR_CODE_TASK_NO_EXIST;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task stop task end.");

    return NGX_MEDIA_ERROR_CODE_OK;
}

static int
ngx_media_task_update_task(xmlDocPtr doc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task update task end.");
    return NGX_MEDIA_ERROR_CODE_OK;//todo:
}



static void
ngx_media_task_start_worker(ngx_str_t* taskid,ngx_str_t* workerid)
{

    ngx_media_task_t     *task   = NULL;
    ngx_media_task_t     *find   = NULL;
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t               *part   = NULL;
    ngx_int_t                      start  = 0;

    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return ;
    }

    find = video_task_ctx.task_head;

    while(NULL != find) {
        if(ngx_strncmp(taskid->data,find->task_id.data,find->task_id.len) == 0) {
            task = find;
            break;
        }
        find = find->next_task;
    }

    if(NULL == task) {
        ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);
        return ;
    }
    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);
        return ;
    }
    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            if(0 != ngx_strncmp(worker->wokerid.data, workerid->data, workerid->len)) {
                continue;
            }
            start = 1;
            if(ngx_thread_mutex_lock(&worker->work_mtx, worker->log) == NGX_OK) {

                if((ngx_media_worker_status_init == worker->status)
                    &&(NULL !=  worker->worker)) {
                    if(NGX_OK != worker->worker->start_worker(worker)) {
                        ngx_log_error(NGX_LOG_ERR, task->log, 0,
                                     "ngx http video task trigger start task worker fail.");
                    }
                    ngx_log_error(NGX_LOG_DEBUG, task->log, 0,
                               "ngx http video task trigger start task:[%V] worker:[%V]",&task->task_id,&worker->wokerid);
                }
                ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
            }
            break;
        }
        if(start) {
            break;
        }
        part = part->next;
    }

    ngx_thread_mutex_unlock(&task->task_mtx, task->log);

    ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);

    return;

}


static void
ngx_media_task_worker_trigger(ngx_uint_t status,ngx_media_worker_ctx_t* ctx)
{
    ngx_media_worker_trigger_ctx   *trigger = NULL;
    ngx_media_worker_trigger_ctx   *array   = NULL;
    ngx_list_part_t               *part    = NULL;
    ngx_uint_t                     loop    = 0;
    if(ngx_media_worker_status_start == status) {
        part  = &(ctx->triggerStart->part);
    }
    else if(ngx_media_worker_status_end == status) {
        part  = &(ctx->triggerEnd->part);
    }
    else {
        return;
    }

    while (part)
    {
        array = (ngx_media_worker_trigger_ctx*)(part->elts);
        loop = 0;
        for (; loop < part->nelts; loop++)
        {
            trigger = &array[loop];
            /* try to start the worker by the trigger */
            ngx_media_task_start_worker(&trigger->taskid,&trigger->wokerid);
        }
        part = part->next;
    }

}

static void
ngx_media_task_worker_watcher(ngx_uint_t status,ngx_int_t err_code,ngx_media_worker_ctx_t* ctx)
{

    if((ngx_media_worker_status_start == status)
        ||(ngx_media_worker_status_end == status)) {
            ngx_media_task_worker_trigger(status,ctx);
    }
    if(ngx_thread_mutex_lock(&ctx->work_mtx, ctx->log) == NGX_OK) {
        ctx->status      = status;
        ctx->error_code  = err_code;
        ctx->updatetime  = ngx_time();
        ngx_thread_mutex_unlock(&ctx->work_mtx, ctx->log);
    }
}

static void
ngx_media_task_dump_info(ngx_media_task_t* task)
{
    ngx_media_worker_ctx_t *worker     = NULL;
    ngx_media_worker_ctx_t *array      = NULL;
    ngx_list_part_t             *part       = NULL;
    u_char                      *arg        = NULL;
    u_char                      *value      = NULL;


    /* start the master worker first */
    ngx_log_error(NGX_LOG_DEBUG, task->log, 0,
                          "dump the task:[%V]:",&task->task_id);
    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            ngx_log_error(NGX_LOG_DEBUG, task->log, 0,
                          "\t the worker:[%V],params:",&worker->wokerid);
            ngx_int_t i = 0;
            for(;i < worker->nparamcount;i++) {
                arg = worker->paramlist[i];
                value = NULL;
                if((i + 1) < worker->nparamcount) {
                    value = worker->paramlist[i+1];
                }

                if(NULL != value) {
                   if('-' != value[0]) {
                        ngx_log_error(NGX_LOG_DEBUG, task->log, 0,
                               "\t\t arg:[%s] value:[%s]",arg,value);
                        i++;
                        continue;
                   }
                }
                ngx_log_error(NGX_LOG_DEBUG, task->log, 0,
                               "\t\t arg:[%s]",arg);
            }
        }
        part = part->next;
    }
}

static ngx_int_t
ngx_media_task_check_static_task_tree_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}

static ngx_int_t
ngx_media_task_deal_static_xml(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_task_deal_static_xml,file:[%V].",path);

    xmlDocPtr  doc;
    int        ret      = NGX_MEDIA_ERROR_CODE_OK;
    xmlNodePtr curNode  = NULL;
    u_char*    pCommand = NULL;
    ngx_media_main_conf_t* conf;
    conf = (ngx_media_main_conf_t*)ctx->data;

    xmlKeepBlanksDefault(0);
    doc = xmlParseFile((char*)path->data);
    if(NULL == doc)
    {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "http video task deal the xml fail.");
        return NGX_OK;
    }

    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode)
    {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "http video task get the xml root fail.");
        xmlFreeDoc(doc);
        return NGX_OK;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST "req"))
    {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "http video task find the xml req node fail.");
        xmlFreeDoc(doc);
        return NGX_OK;
    }

    curNode = curNode->children;

    if (xmlStrcmp(curNode->name, BAD_CAST "task"))
    {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "http video task find the xml task node fail,:%s.",curNode->name);
        xmlFreeDoc(doc);
        return NGX_OK;
    }

    pCommand = xmlGetProp(curNode,BAD_CAST "command");
    if(NULL == pCommand){
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "http video task find the xml command attribute from task node fail.");
        xmlFreeDoc(doc);
        return NGX_OK;
    }

    if(ngx_strncmp(pCommand,TASK_COMMAND_START,ngx_strlen(TASK_COMMAND_START)) == 0) {
       ret = ngx_media_task_start_task(conf,doc);
    }
    else {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "http video task unknow the command type.");
    }

    if(NGX_MEDIA_ERROR_CODE_OK != ret) {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "ngx http video task deal xml req error,ret:%d.",ret);
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, conf->log, 0,
                          "ngx http video task deal xml req,ret:%d.",ret);
    xmlFreeDoc(doc);

    return NGX_OK;
}


static void
ngx_media_task_check_static_task(ngx_event_t *ev)
{
    ngx_file_info_t           fi;
    ngx_tree_ctx_t            tree;

    ngx_media_main_conf_t* conf
            = (ngx_media_main_conf_t*)ev->data;
    if(NULL == conf->static_task.data|| 0 == conf->static_task.len) {
        ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "not configuer the static work path.");
        return;
    }

    /* check all the task xml file,and start the task */
    if (ngx_link_info(conf->static_task.data, &fi) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_WARN, conf->log, 0,
                          "ngx_media_task_check_static_task,link the dir:[%V] fail.",&conf->static_task);
        return;
    }
    if (!ngx_is_dir(&fi)) {
        ngx_log_error(NGX_LOG_WARN, conf->log, 0,
                          "ngx_media_task_check_static_task,the:[%V] is not a dir.",&conf->static_task);
        return;
    }

    /* walk the static task dir */
    tree.init_handler = NULL;
    tree.file_handler = ngx_media_task_deal_static_xml;
    tree.pre_tree_handler = ngx_media_task_check_static_task_tree_noop;
    tree.post_tree_handler = ngx_media_task_check_static_task_tree_noop;
    tree.spec_handler = ngx_media_task_check_static_task_tree_noop;
    tree.data = conf;
    tree.alloc = 0;
    tree.log =conf->log;
    if (ngx_walk_tree(&tree, &conf->static_task) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                          "ngx_media_worker_transfer_delete,walk tree:[%V] fail.",&conf->static_task);
        return;
    }

    ngx_add_timer(&conf->static_task_timer, NGX_HTTP_STATIC_TASK_TIMER);

    return;
}


static void
ngx_media_task_check_workers(ngx_media_task_t* task)
{
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t          *part   = NULL;
    ngx_uint_t                status = ngx_media_worker_status_end;
    ngx_int_t                 err_code  = NGX_MEDIA_ERROR_CODE_OK;

    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        return ;
    }
    part  = &(task->workers->part);
    while (part)
    {
        array = (ngx_media_worker_ctx_t*)(part->elts);
        ngx_uint_t loop = 0;
        for (; loop < part->nelts; loop++)
        {
            worker = &array[loop];
            if(ngx_thread_mutex_lock(&worker->work_mtx, worker->log) == NGX_OK) {
                if(worker->error_code > err_code) {
                    err_code = worker->error_code;
                }
                if(ngx_media_worker_status_break == worker->status) {
                    status = ngx_media_worker_status_break;
                    ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
                    goto end;
                }
                else if(ngx_media_worker_status_end != worker->status) {
                    /* try to start the next worker */
                    status = ngx_media_worker_status_running;
                }
                ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
            }
        }
        part = part->next;
    }
end:
    task->status     = status;
    task->error_code = err_code;

    ngx_thread_mutex_unlock(&task->task_mtx, task->log);
    return;
}

static void
ngx_media_task_check_task(ngx_event_t *ev)
{
    ngx_media_task_t* task = (ngx_media_task_t*)ev->data;

    ngx_media_task_check_workers(task);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task check task:[%V],status:[%d] start.",
                          &task->task_id,task->status);

    time_t now = ngx_time();

    if(ngx_media_worker_status_running == task->status) {
        if((now > task->lastreport)
            &&((now - task->lastreport) > TASK_INVALIED_TIME)) {
            ngx_media_task_report_progress(task);
            task->lastreport = now;
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task report task:[%V] running progress.",&task->task_id);
        }
    } else{
        ngx_media_task_report_progress(task);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task report task:[%V] stop progress.",&task->task_id);
    }
    if((ngx_media_worker_status_end== task->status)
        ||(ngx_media_worker_status_break== task->status)){
        /*stop the task  and delete the task */
        ngx_media_task_destory_task(task);
        return;
    }


    ngx_add_timer(&task->time_event, TASK_CHECK_INTERVAL);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, video_task_ctx.log, 0,
                          "ngx http video task check task end.");
    return;
}

static void
ngx_media_task_dump_xml_to_file(ngx_http_request_t *r,ngx_str_t* taskid,xmlDocPtr doc)
{
    ngx_media_main_conf_t* conf;
    u_char path[TRANS_STRING_MAX_LEN];

    ngx_memzero(&path, TRANS_STRING_MAX_LEN);
    conf = ngx_http_get_module_main_conf(r, ngx_media_task_module);

    if((NULL == conf->static_task.data )||( 0 == conf->static_task.len)) {
        return;
    }

    u_char* last = ngx_snprintf(path, TRANS_STRING_MAX_LEN,"%V/%V.xml", &conf->static_task,taskid);
    *last ='\0';
    if (NULL != doc) {
        xmlSaveFormatFile((char*)&path[0], doc, 1);
    }
    return;
}

static void
ngx_media_task_deal_xml_req(ngx_http_request_t *r,const char* req_xml,ngx_chain_t* out)
{
    xmlDocPtr doc;

    int        ret      = NGX_MEDIA_ERROR_CODE_OK;
    xmlNodePtr curNode  = NULL;
    u_char*    pTaskID  = NULL;
    ngx_str_t  taskid;
    u_char*    pCommand = NULL;
    u_char*    pType    = NULL;
    ngx_uint_t bStatic  = 0;
    ngx_media_main_conf_t* conf;


    conf = ngx_http_get_module_main_conf(r, ngx_media_task_module);

    xmlKeepBlanksDefault(0);
    doc = xmlReadDoc((const xmlChar *)req_xml,"msg_req.xml",NULL,0);
    if(NULL == doc)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video task deal the xml fail.");
        ngx_media_task_error_xml(r,out,NGX_MEDIA_ERROR_CODE_XML_ERROR,NULL);
        return ;
    }


    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video task get the xml root fail.");
        ngx_media_task_error_xml(r,out,NGX_MEDIA_ERROR_CODE_XML_ERROR,NULL);
        xmlFreeDoc(doc);
        return ;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST "req"))
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video task find the xml req node fail.");
        ngx_media_task_error_xml(r,out,NGX_MEDIA_ERROR_CODE_XML_ERROR,NULL);
        xmlFreeDoc(doc);
        return ;
    }

    curNode = curNode->children;

    if (xmlStrcmp(curNode->name, BAD_CAST "task"))
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video task find the xml task node fail,:%s.",curNode->name);
        ngx_media_task_error_xml(r,out,NGX_MEDIA_ERROR_CODE_PARAM_ERROR,NULL);
        xmlFreeDoc(doc);
        return ;
    }


    pTaskID = xmlGetProp(curNode,BAD_CAST "taskid");
    if(NULL == pTaskID){
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video task find the xml taskid attribute from task node fail.");
        ngx_media_task_error_xml(r,out,NGX_MEDIA_ERROR_CODE_PARAM_ERROR,NULL);
        xmlFreeDoc(doc);
        return ;
    }
    taskid.data = ngx_pcalloc(r->pool, ngx_strlen(pTaskID) + 1);
    u_char* last = ngx_copy(taskid.data,pTaskID,ngx_strlen(pTaskID));
    *last = '\0';
    taskid.len = ngx_strlen(pTaskID);
    pCommand = xmlGetProp(curNode,BAD_CAST "command");
    if(NULL == pCommand){
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video task find the xml command attribute from task node fail.");
        xmlFreeDoc(doc);
        return ;
    }

    pType = xmlGetProp(curNode,BAD_CAST COMMON_XML_REQ_TYPE);
    if(NULL != pType){
        if(ngx_strncmp(pType,COMMON_XML_REQ_STATIC,ngx_strlen(COMMON_XML_REQ_STATIC)) == 0) {
           bStatic = 1;
        }
    }

    if(ngx_strncmp(pCommand,TASK_COMMAND_START,ngx_strlen(TASK_COMMAND_START)) == 0) {
        ret = ngx_media_task_start_task(conf,doc);
        if(bStatic &&(NGX_MEDIA_ERROR_CODE_OK == ret)) {
            ngx_media_task_dump_xml_to_file(r,&taskid,doc);
        }
    }
    else if(ngx_strncmp(pCommand,TASK_COMMAND_STOP,ngx_strlen(TASK_COMMAND_STOP)) == 0) {
        ret = ngx_media_task_stop_task(&taskid);
    }
    else if(ngx_strncmp(pCommand,TASK_COMMAND_UPDATE,ngx_strlen(TASK_COMMAND_UPDATE)) == 0) {
        ret = ngx_media_task_update_task(doc);
    }
    else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video task unknow the command type.");
        ret = NGX_MEDIA_ERROR_CODE_SYS_ERROR;
    }

    ngx_media_task_error_xml(r,out,ret,curNode);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                          "ngx http video task deal xml req,ret:%d.",ret);
    xmlFreeDoc(doc);
    return;
}

static void
ngx_media_task_recv_request(ngx_http_request_t *r)
{
    u_char       *p;
    u_char       *req;
    size_t        len;
    ngx_buf_t    *buf;
    ngx_chain_t  *cl;
    ngx_chain_t  out;

    if (r->request_body == NULL
        || r->request_body->bufs == NULL
        || r->request_body->temp_file)
    {
        return ;
    }

    cl = r->request_body->bufs;
    buf = cl->buf;

    len = buf->last - buf->pos;
    cl = cl->next;

    for ( /* void */ ; cl; cl = cl->next) {
        buf = cl->buf;
        len += buf->last - buf->pos;
    }

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video task ,alloc the buf fail.");
        return ;
    }
    req = p;

    cl = r->request_body->bufs;
    for ( /* void */ ; cl; cl = cl->next) {
        buf = cl->buf;
        p = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
    }

    *p = '\0';

    //r->request_body
    out.buf = NULL;
    ngx_media_task_deal_xml_req(r,(const char * )req,&out);

    r->headers_out.status = NGX_HTTP_OK;
    r->header_only = 1;
    r->keepalive   = 0;

    if(NULL == out.buf) {
        r->header_only = 1;
        ngx_http_finalize_request(r, ngx_http_send_header(r));
    }
    else {
        r->header_only = 0;
        ngx_int_t rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            ngx_http_finalize_request(r,rc);
            return;
        }

        ngx_http_finalize_request(r,ngx_http_output_filter(r, &out));
    }

    return ;
}

ngx_uint_t
ngx_media_task_get_cur_count()
{
    ngx_uint_t count = 0;
    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return count;
    }
    count = video_task_ctx.task_count;
    if (ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return count;
    }
    return count;
}





