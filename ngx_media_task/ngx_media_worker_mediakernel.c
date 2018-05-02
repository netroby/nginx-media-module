#include "ngx_media_worker_mediakernel.h"

#define NGX_HTTP_VIDEO_ARG_MK_SRC    "-src"
#define NGX_HTTP_VIDEO_ARG_MK_DST    "-dst"

#define NGG_HTTP_VIDEO_MK_TIME 5000

static void
ngx_media_worker_mk_unescape_uri(ngx_media_worker_ctx_t* ctx)
{
    ngx_int_t i        = 0;
    u_char   *arg      = NULL;
    u_char   *value    = NULL;
    u_char   *unescape = NULL;

    for(i = 0;i < ctx->nparamcount;i++) {

        arg = ctx->paramlist[i];
        if((0 != ngx_strncmp(arg,NGX_HTTP_VIDEO_ARG_MK_SRC,ngx_strlen(NGX_HTTP_VIDEO_ARG_MK_SRC)))
            &&(0 != ngx_strncmp(arg,NGX_HTTP_VIDEO_ARG_MK_DST,ngx_strlen(NGX_HTTP_VIDEO_ARG_MK_DST)))) {
            continue;
        }
        if((i + 1) >= ctx->nparamcount) {
            continue;
        }

        value = ctx->paramlist[i+1];

        ngx_uint_t lens  = ngx_strlen(value);
        unescape = ngx_pcalloc(ctx->pool,lens+1);
        u_char* pszDst = unescape;
        ngx_unescape_uri(&pszDst,&value, lens, 0);
        pszDst = '\0';
        ctx->paramlist[i+1] = unescape;
        ngx_pfree(ctx->pool,value);
    }
}

static void
ngx_media_worker_mk_timer(ngx_event_t *ev)
{
    int status = MK_TASK_STATUS_INIT;
    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ev->data;
    if(NULL == worker_ctx->watcher) {
        return;
    }
    int32_t  ret = mk_get_task_status(worker_ctx->run_handle,&status,&worker_ctx->worker_stat);
    if(MK_ERROR_CODE_OK != ret) {
        return;
    }
    if(MK_TASK_STATUS_INIT == status) {
        worker_ctx->watcher(ngx_media_worker_status_init,worker_ctx->wk_ctx);
    }
    else if(MK_TASK_STATUS_START == status) {
        worker_ctx->watcher(ngx_media_worker_status_start,worker_ctx->wk_ctx);
    }
    else if(MK_TASK_STATUS_RUNNING == status) {
        worker_ctx->watcher(ngx_media_worker_status_running,worker_ctx->wk_ctx);
    }
    else if(MK_TASK_STATUSS_STOP == status) {
        worker_ctx->watcher(ngx_media_worker_status_end,worker_ctx->wk_ctx);
    }
    else {
        worker_ctx->watcher(ngx_media_worker_status_break,worker_ctx->wk_ctx);
    }

    if(MK_TASK_STATUSS_STOP == status) {
        return;
    }
    ngx_add_timer(&worker_ctx->timer,NGG_HTTP_VIDEO_MK_TIME);
}



static ngx_int_t
ngx_media_worker_mk_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch)
{
    ngx_uint_t lens = sizeof(ngx_worker_mk_ctx_t);
    ngx_worker_mk_ctx_t *worker_ctx = ngx_pcalloc(ctx->pool,lens);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                          "ngx_media_worker_mk_init begin");

    if(NULL == worker_ctx) {
       ngx_log_debug0(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_mk_init allocate mk ctx fail.");
        return NGX_ERROR;
    }

    ngx_media_worker_mk_unescape_uri(ctx);

    ctx->priv_data_size = lens;
    ctx->priv_data      = worker_ctx;

    worker_ctx->run_handle = mk_create_handle();
    worker_ctx->watcher    = watch;
    worker_ctx->wk_ctx     = ctx;
    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_mk_release(ngx_media_worker_ctx_t* ctx)
{
    if(ctx->priv_data_size != sizeof(ngx_worker_mk_ctx_t)) {
        return NGX_OK;
    }

    if(NULL == ctx->priv_data) {
        return NGX_OK;
    }
    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        mk_destory_handle(worker_ctx->run_handle);
        worker_ctx->run_handle = NULL;
    }
    ngx_pfree(ctx->pool, ctx->priv_data);
    ctx->priv_data      = NULL;
    ctx->priv_data_size = 0;
    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mk_start(ngx_media_worker_ctx_t* ctx)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mk_start worker:[%V] begin.",&ctx->wokerid);
    if(ctx->priv_data_size != sizeof(ngx_worker_mk_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ctx->priv_data;

    int32_t ret  = mk_run_task(worker_ctx->run_handle,ctx->nparamcount,(char**)ctx->paramlist);

    if (MK_ERROR_CODE_OK != ret ) {
        mk_destory_handle(worker_ctx->run_handle);
        worker_ctx->run_handle = NULL;
        return NGX_ERROR;
    }

    ngx_memzero(&worker_ctx->timer, sizeof(ngx_event_t));
    worker_ctx->timer.handler = ngx_media_worker_mk_timer;
    worker_ctx->timer.log     = ctx->log;
    worker_ctx->timer.data    = worker_ctx;

    ngx_add_timer(&worker_ctx->timer,NGG_HTTP_VIDEO_MK_TIME);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mk_start worker:[%V] end.",&ctx->wokerid);

    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mk_stop(ngx_media_worker_ctx_t* ctx)
{

    if(ctx->priv_data_size != sizeof(ngx_worker_mk_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        mk_stop_task(worker_ctx->run_handle);
    }
    //ngx_del_timer(&worker_ctx->timer);
    return NGX_OK;
}


/* media kernel worker */
ngx_media_worker_t ngx_video_mediakernel_worker = {
    .type             = ngx_media_worker_mediakernel,
    .init_worker      = ngx_media_worker_mk_init,
    .release_worker   = ngx_media_worker_mk_release,
    .start_worker     = ngx_media_worker_mk_start,
    .stop_worker      = ngx_media_worker_mk_stop,
};



