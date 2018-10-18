#ifndef __NGX_MEDIA_WORKER_MEDIAKERNEL_H__
#define __NGX_MEDIA_WORKER_MEDIAKERNEL_H__
#include "ngx_media_worker.h"
#include "libASRtsp2RtmpClient.h"
#include "as_def.h"

typedef struct {
    AS_HANDLE                       run_handle;
    ngx_str_t                       src;
    ngx_str_t                       dst;
    ngx_uint_t                      transport;
    WK_WATCH                        watcher;
    ngx_media_worker_ctx_t         *wk_ctx;
    ngx_event_t                     timer;
} ngx_worker_access_ctx_t;

static ngx_int_t    ngx_media_worker_access_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch);
static ngx_int_t    ngx_media_worker_access_release(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_access_start(ngx_media_worker_ctx_t* ctx);
static ngx_int_t    ngx_media_worker_access_stop(ngx_media_worker_ctx_t* ctx);
#endif /*__NGX_MEDIA_WORKER_MEDIAKERNEL_H__*/