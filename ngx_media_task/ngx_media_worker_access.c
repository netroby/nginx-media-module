#include "ngx_media_worker_access.h"


#define NGX_HTTP_VIDEO_ARG_ACCESS_SRC              "-src"
#define NGX_HTTP_VIDEO_ARG_ACCESS_RTSPTRANSPORT    "-rtsp_transport"
#define NGX_HTTP_VIDEO_ARG_ACCESS_DST              "-dst"

#define NGG_HTTP_VIDEO_MK_TIME 5000

static ngx_int_t
ngx_media_worker_access_parser_args(ngx_media_worker_ctx_t* ctx)
{
    ngx_int_t i        = 0;
    u_char   *arg      = NULL;
    u_char   *value    = NULL;
    ngx_uint_t lens    = 0;
    u_char   *unescape = NULL;

    ngx_worker_access_ctx_t *worker_ctx = (ngx_worker_access_ctx_t *)ctx->priv_data;

    for(i = 0;i < ctx->nparamcount;i++) {

        arg   = ctx->paramlist[i];
        value = NULL;
        if((i + 1) < ctx->nparamcount) {
            value = ctx->paramlist[i+1];
            lens  = ngx_strlen(value);
        }
        if((0 == ngx_strncmp(arg,NGX_HTTP_VIDEO_ARG_ACCESS_SRC,ngx_strlen(NGX_HTTP_VIDEO_ARG_ACCESS_SRC))) {
            if(NULL == value) {
                return NGX_ERROR;
            }
            unescape = ngx_pcalloc(ctx->pool,lens+1);
            u_char* pszDst = unescape;
            ngx_unescape_uri(&pszDst,&value, lens, 0);
            pszDst = '\0';
            worker_ctx->src.data = unescape;
            worker_ctx->src.len  = lens+1;
        }
        else if(0 == ngx_strncmp(arg,NGX_HTTP_VIDEO_ARG_ACCESS_DST,ngx_strlen(NGX_HTTP_VIDEO_ARG_ACCESS_DST))) {
            if(NULL == value) {
                return NGX_ERROR;
            }
            unescape = ngx_pcalloc(ctx->pool,lens+1);
            u_char* pszDst = unescape;
            ngx_unescape_uri(&pszDst,&value, lens, 0);
            pszDst = '\0';
            worker_ctx->dst.data = unescape;
            worker_ctx->dst.len  = lens+1;
        }
        else if(0 == ngx_strncmp(arg,NGX_HTTP_VIDEO_ARG_ACCESS_RTSPTRANSPORT,ngx_strlen(NGX_HTTP_VIDEO_ARG_ACCESS_RTSPTRANSPORT))) {
            if(NULL != value) {
                if(0 == ngx_strncmp(value,"tcp",ngx_strlen("tcp"))) {
                    worker_ctx->transport = 1;
                }
            }
        }
    }

    return NGX_OK;
}


static void
ngx_media_worker_access_timer(ngx_event_t *ev)
{

    ngx_worker_access_ctx_t *worker_ctx = (ngx_worker_access_ctx_t *)ev->data;
    if(NULL == worker_ctx->watcher) {
        return;
    }
    uint32_t  uStatus = as_rtsp2rtmp_get_handle_status(worker_ctx->run_handle);

    if(AS_RTSP_STATUS_INIT == uStatus) {
        worker_ctx->watcher(ngx_media_worker_status_init,error_code,worker_ctx->wk_ctx);
    }
    else if(AS_RTSP_STATUS_SETUP == uStatus) {
        worker_ctx->watcher(ngx_media_worker_status_start,error_code,worker_ctx->wk_ctx);
    }
    else if(AS_RTSP_STATUS_PLAY == uStatus) {
        worker_ctx->watcher(ngx_media_worker_status_running,error_code,worker_ctx->wk_ctx);
    }
    else if(AS_RTSP_STATUS_TEARDOWN == uStatus) {
        worker_ctx->watcher(ngx_media_worker_status_end,error_code,worker_ctx->wk_ctx);
    }

    if(AS_RTSP_STATUS_TEARDOWN == uStatus) {
        return;
    }
    ngx_add_timer(&worker_ctx->timer,NGG_HTTP_VIDEO_MK_TIME);
}



static ngx_int_t
ngx_media_worker_access_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch)
{
    ngx_uint_t lens = sizeof(ngx_worker_access_ctx_t);
    ngx_worker_access_ctx_t *worker_ctx = ngx_pcalloc(ctx->pool,lens);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_access_init begin");

    if(NULL == worker_ctx) {
       ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_access_init allocate access ctx fail.");
        return NGX_ERROR;
    }

    ctx->priv_data_size = lens;
    ctx->priv_data      = worker_ctx;

    worker_ctx->run_handle = NULL;
    worker_ctx->watcher    = watch;
    worker_ctx->wk_ctx     = ctx;
    return ngx_media_worker_access_parser_args(ctx);
}

static ngx_int_t
ngx_media_worker_access_release(ngx_media_worker_ctx_t* ctx)
{
    if(ctx->priv_data_size != sizeof(ngx_worker_access_ctx_t)) {
        return NGX_OK;
    }

    if(NULL == ctx->priv_data) {
        return NGX_OK;
    }
    ngx_worker_access_ctx_t *worker_ctx = (ngx_worker_access_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        as_rtsp2rtmp_destory_handle(worker_ctx->run_handle);
        worker_ctx->run_handle = NULL;
    }
    ngx_pfree(ctx->pool, ctx->priv_data);
    ctx->priv_data      = NULL;
    ctx->priv_data_size = 0;
    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_access_start(ngx_media_worker_ctx_t* ctx)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_access_start worker:[%V] begin.",&ctx->wokerid);
    if(ctx->priv_data_size != sizeof(ngx_worker_access_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_access_ctx_t *worker_ctx = (ngx_worker_access_ctx_t *)ctx->priv_data;

    worker_ctx->run_handle  = as_rtsp2rtmp_create_handle(worker_ctx->src.data,worker_ctx->dst.data,worker_ctx->transport);

    if (NULL == worker_ctx->run_handle ) {
        worker_ctx->run_handle = NULL;
        return NGX_ERROR;
    }

    ngx_memzero(&worker_ctx->timer, sizeof(ngx_event_t));
    worker_ctx->timer.handler = ngx_media_worker_access_timer;
    worker_ctx->timer.log     = ctx->log;
    worker_ctx->timer.data    = worker_ctx;

    ngx_add_timer(&worker_ctx->timer,NGG_HTTP_VIDEO_MK_TIME);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_access_start worker:[%V] end.",&ctx->wokerid);

    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_access_stop(ngx_media_worker_ctx_t* ctx)
{

    if(ctx->priv_data_size != sizeof(ngx_worker_access_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_access_ctx_t *worker_ctx = (ngx_worker_access_ctx_t *)ctx->priv_data;
    if(worker_ctx->run_handle){
        as_rtsp2rtmp_destory_handle(worker_ctx->run_handle);
    }
    return NGX_OK;
}


/* rtsp factory worker */
ngx_media_worker_t ngx_video_rtspfactory_worker = {
    .type             = ngx_media_worker_access,
    .init_worker      = ngx_media_worker_access_init,
    .release_worker   = ngx_media_worker_access_release,
    .start_worker     = ngx_media_worker_access_start,
    .stop_worker      = ngx_media_worker_access_stop,
};



