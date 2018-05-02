#include "ngx_media_worker_mss.h"
#include "ngx_http_client.h"
#include "ngx_http_digest.h"
#include <cjson/cJSON.h>


#define MSS_WORK_ARG_PREF            "-mss_"
#define MSS_WORK_ARG_ADDR            "-mss_addr"
#define MSS_WORK_ARG_CAMERAID        "-mss_cameraid"
#define MSS_WORK_ARG_TYPE            "-mss_type"
#define MSS_WORK_ARG_STARTTIME       "-mss_starttime"
#define MSS_WORK_ARG_ENDTIME         "-mss_endtime"

#define MSS_WORK_MEDIA_TYPE_LIVE     "live"
#define MSS_WORK_MEDIA_TYPE_RECORD   "record"


enum MSS_MEDIA_TYE
{
    MSS_MEDIA_TYE_LIVE    = 0,
    MSS_MEDIA_TYE_RECORD  = 1,
    MSS_MEDIA_TYE_MAX
};

typedef enum NGX_MEDIA_WOKER_MSS_STATUS
{
    NGX_MEDIA_WOKER_MSS_STATUS_INIT    = 0,
    NGX_MEDIA_WOKER_MSS_STATUS_REQ_URL = 1,
    NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN  = 2,
    NGX_MEDIA_WOKER_MSS_STATUS_BREAK   = 3,
    NGX_MEDIA_WOKER_MSS_STATUS_END
}WOKER_MSS_STATUS;

typedef struct {
    ngx_str_t                       mss_addr;
    ngx_str_t                       mss_name;
    ngx_str_t                       mss_passwd;
    ngx_str_t                       mss_cameraid;
    ngx_str_t                       mss_type;
    ngx_str_t                       mss_streamtype;   /* for live */
    ngx_str_t                       mss_contentid;
    ngx_str_t                       mss_starttime;
    ngx_str_t                       mss_endtime;
    ngx_str_t                       mss_nvrcode;
} ngx_worker_mss_arg_t;

typedef struct {
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    WK_WATCH                        watcher;
    ngx_media_worker_ctx_t         *wk_ctx;
    ngx_event_t                     timer;
    WOKER_MSS_STATUS                status;
    /* mss http request */
    ngx_worker_mss_arg_t            mss_arg;
    ngx_http_request_t             *mss_req;
    ngx_digest_t                    mss_digest;
    ngx_str_t                       mss_req_msg;
    ngx_str_t                       mss_resp_msg;
    /* mk context */
    MK_HANDLE                       run_handle;
    mk_task_stat_info_t             worker_stat;
    ngx_int_t                       mk_nparamcount;
    u_char**                        mk_paramlist;
} ngx_worker_mss_ctx_t;


#define NGX_HTTP_VIDEO_MSS_TIME 5000
#define NGX_MSS_ERROR_CODE_OK   "00000000"

enum NGX_MEDIA_WORKER_MSS_HTTP_HEADER
{
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_TYPE    = 0,
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_LENGHT  = 1,
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_AUTH    = 2,
    NGX_MEDIA_WORKER_MSS_HTTP_HEADER_MAX
};


static void
ngx_media_worker_mss_unescape_uri(ngx_media_worker_ctx_t* ctx)
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
ngx_media_worker_mss_args(ngx_worker_mss_ctx_t* mss_ctx,u_char* arg,u_char* value,ngx_int_t* index)
{
    ngx_int_t ret = NGX_OK;
    size_t size = ngx_strlen(arg);
    if(ngx_strncmp(arg,MSS_WORK_ARG_ADDR,size) == 0) {
        ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_addr);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_CAMERAID,size) == 0) {
        ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_cameraid);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_TYPE,size) == 0) {
        ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_type);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_STARTTIME,size) == 0) {
        ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_starttime);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else if(ngx_strncmp(arg,MSS_WORK_ARG_ENDTIME,size) == 0) {
        ret = ngx_media_worker_arg_value_str(mss_ctx->pool,value,&mss_ctx->mss_arg.mss_endtime);
        if(NGX_OK == ret) {
            (*index)++;
        }
    }
    else {
        return NGX_ERROR;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mss_parser_args(ngx_media_worker_ctx_t* ctx)
{
    ngx_worker_mss_ctx_t* mss_ctx = (ngx_worker_mss_ctx_t*)ctx->priv_data;

    ngx_int_t ret = NGX_OK;
    ngx_int_t i = 0;

    ngx_uint_t lens  = sizeof(u_char*)*(ctx->nparamcount + 4);

    mss_ctx->mk_paramlist    = ngx_pcalloc(ctx->pool,lens);
    mss_ctx->mk_nparamcount  = 0;

    mss_ctx->mk_paramlist[0] = "-rtsp_transport";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[1] = "tcp";
    mss_ctx->mk_nparamcount++;

    mss_ctx->mk_paramlist[2] = "-src";
    mss_ctx->mk_nparamcount++;
    mss_ctx->mk_paramlist[3] = "tmp"; /* replace by the mms return rtsp url */
    mss_ctx->mk_nparamcount++;

    for(i = 0; i < ctx->nparamcount;i++) {
        u_char* arg   = ctx->paramlist[i];
        u_char* value = NULL;
        if(i < (ctx->nparamcount -1)) {
            value = ctx->paramlist[i+1];
        }

        if(ngx_strncmp(arg,MSS_WORK_ARG_PREF,ngx_strlen(MSS_WORK_ARG_PREF)) == 0) {
            ngx_media_worker_mss_args(mss_ctx,arg,value,&i);
        }
        else
        {
            mss_ctx->mk_paramlist[mss_ctx->mk_nparamcount] = arg;
            mss_ctx->mk_nparamcount++;
        }
    }
    return NGX_OK;
}

static void
ngx_media_worker_mss_timer(ngx_event_t *ev)
{
    int status = MK_TASK_STATUS_INIT;
    ngx_worker_mk_ctx_t *worker_ctx = (ngx_worker_mk_ctx_t *)ev->data;
    if(NULL == worker_ctx->watcher) {
        return;
    }

    if(NGX_MEDIA_WOKER_MSS_STATUS_INIT == worker_ctx->status ) {
        worker_ctx->watcher(ngx_media_worker_status_init,worker_ctx->wk_ctx);
    }
    else if(NGX_MEDIA_WOKER_MSS_STATUS_REQ_URL == worker_ctx->status ) {
        worker_ctx->watcher(ngx_media_worker_status_running,worker_ctx->wk_ctx);
    }
    else if(NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN == worker_ctx->status ) {
        int32_t  ret = mk_get_task_status(worker_ctx->run_handle,&status,&worker_ctx->worker_stat);
        if(MK_ERROR_CODE_OK != ret) {
            return;
        }
        if(MK_TASK_STATUS_INIT == status) {
        worker_ctx->watcher(ngx_media_worker_status_running,worker_ctx->wk_ctx);
        }
        else if(MK_TASK_STATUS_START == status) {
            worker_ctx->watcher(ngx_media_worker_status_running,worker_ctx->wk_ctx);
        }
        else if(MK_TASK_STATUS_RUNNING == status) {
            worker_ctx->watcher(ngx_media_worker_status_running,worker_ctx->wk_ctx);
        }
        else if(MK_TASK_STATUSS_STOP == status) {
            worker_ctx->watcher(ngx_media_worker_status_end,worker_ctx->wk_ctx);
            worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_END;
        }
        else {
            worker_ctx->watcher(ngx_media_worker_status_break,worker_ctx->wk_ctx);
            worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
        }

    }

    if(NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN < worker_ctx->status {
        return;
    }
    ngx_add_timer(&worker_ctx->timer,NGX_HTTP_VIDEO_MSS_TIME);
}

static void
ngx_media_worker_mss_start_media_kernel(ngx_worker_mss_ctx_t *worker_ctx)
{
    ngx_flag_t flag = 0;
    /* 1.parse the response */
    cJSON root = cJSON_Parse(worker_ctx->mss_resp_msg.data);
    if (NULL == root) {
        ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,"ngx media worker mss start media kernel, json message parser fail.");
        return;
    }
    do {
        cJSON *resultCode = cJSON_GetObjectItem(root, "resultCode");
        if(NULL == resultCode) {
            ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,"ngx media worker mss start media kernel, json message there is no resultCode.");
            break;
        }

        if(0 != ngx_strncmp(NGX_MSS_ERROR_CODE_OK,resultCode->valuestring,ngx_strlen(NGX_MSS_ERROR_CODE_OK))) {
            ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,"ngx media worker mss start media kernel, resultCode:[%s] is not success.",resultCode->valuestring);
            break;
        }

        cJSON *url = cJSON_GetObjectItem(root, "url");
        if(NULL == url) {
            ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,"ngx media worker mss start media kernel, json message there is no url.");
            break;
        }
        ngx_uint_t lens  = ngx_strlen(url->valuestring);
        worker_ctx->mk_paramlist[3] = ngx_pcalloc(worker_ctx->pool,lens + 1);
        u_char* last = ngx_cpymem(worker_ctx->mk_paramlist[3], url->valuestring,lens);
        *last = '\0';
        flag = 1;
    }
    cJSON_Delete(root);

    if(!flag) {
        worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
        return;
    }

    /* 2. start the MK task */
    worker_ctx->run_handle = mk_create_handle();
    int32_t ret  = mk_run_task(worker_ctx->run_handle,worker_ctx->mk_nparamcount,(char**)worker_ctx->mk_paramlist);

    if (MK_ERROR_CODE_OK != ret ) {
        mk_destory_handle(worker_ctx->run_handle);
        worker_ctx->run_handle = NULL;
        worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
        return;
    }
    worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_MK_RUN;
}

static void
ngx_media_worker_mss_recv_body(void *request, ngx_http_request_t *hcr)
{
    ngx_worker_mss_ctx_t   *worker_ctx = (ngx_worker_mss_ctx_t *)request;
    ngx_http_client_ctx_t  *ctx        = hcr->ctx[0];
    ngx_chain_t            *cl         = NULL;
    ngx_chain_t           **ll;
    ngx_int_t               rc;
    ngx_uint_t              size       = 0;


    ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,"http client test recv body");
    do {
        rc = ngx_http_client_read_body(hcr, &cl, worker_ctx->mss_resp_msg.len);
        ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,"http client test recv body, rc %i %i, %O",rc, ngx_errno, ctx->rbytes);
        if (rc == 0) {
            worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
            break;
        }
        if (rc == NGX_ERROR) {
            worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
            break;
        }
        if (rc == NGX_DONE)
        {
            u_char* last = worker_ctx->mss_resp_msg.data;

            for (ll = &cl; (*ll)->next; ll = &(*ll)->next) {
                size = (*ll)->buf->last - (*ll)->buf->pos;
                last = ngx_cpymem(last, (*ll)->buf->pos, size);
                *last = '\0';
            }
            (*ll)->buf->last_buf = 1;
            ngx_media_worker_mss_start_media_kernel(worker_ctx);
        }
    }while(false);

    ngx_http_client_finalize_request(hcr, 1);
    return;
}
static void
ngx_media_worker_mss_recv(void *request, ngx_http_request_t *hcr)
{
    ngx_worker_mss_ctx_t   *worker_ctx = (ngx_worker_mss_ctx_t *)request;
    ngx_http_client_ctx_t  *ctx;
    static ngx_str_t        www_auth     = ngx_string("WWW-Authenticate");
    static ngx_str_t        cont_lenth   = ngx_string("Content-length");
    ngx_str_t              *auth         = NULL;
    ngx_str_t              *cl           = NULL;
    ngx_uint_t              status_code  = NGX_HTTP_OK;
    ctx = hcr->ctx[0];

    ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0, "ngx media worker mss handle recv");

    ctx->read_handler = ngx_media_worker_mss_recv_body;

    ngx_log_error(NGX_LOG_INFO, worker_ctx->log, 0,
                         "ngx media worker mss status code: %ui   http_version: %ui",
                         ngx_http_client_status_code(hcr),
                         ngx_http_client_http_version(hcr));

    if(NGX_HTTP_UNAUTHORIZED == status_code) {
        /* need authortized */
        auth = ngx_http_client_header_in(hcr, &www_auth);
        if(NULL == auth) {
            ngx_log_error(NGX_LOG_ERR, worker_ctx->log, 0, "ngx media worker mss return 401,but no auth head.");
            return;
        }

        if (-1 == ngx_digest_is_digest(auth)) {
            ngx_log_error(NGX_LOG_ERR, worker_ctx->log, 0, "the WWW-Authenticate is not digest.");
            return;
        }

        if (0 == ngx_digest_client_parse(&worker_ctx->mss_digest, auth)) {
            ngx_log_error(NGX_LOG_ERR, worker_ctx->log, 0, "parser WWW-Authenticate info fail.");
            return;
        }

        /* close this request ,and try again */
        (void)ngx_media_worker_mss_url_request(worker_ctx);
        return;
    }
    else if(NGX_HTTP_OK == status_code) {
        cl = ngx_http_client_header_in(hcr, &cont_lenth);
        if(NULL == cl) {
            ngx_log_error(NGX_LOG_ERR, worker_ctx->log, 0, "ngx media worker mss response get content length fail.");
            return;
        }
        worker_ctx->mss_resp_msg.len  = ngx_atoi(cl->data,cl->len);
        worker_ctx->mss_resp_msg.data = ngx_pcalloc(worker_ctx->pool,worker_ctx->mss_resp_msg.len + 1);
        ngx_media_worker_mss_recv_body(request, hcr);
        return;
    }

    /* error request */
    worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
    ngx_http_client_finalize_request(worker_ctx->mss_req,1);
    worker_ctx->mss_req = NULL;

    ngx_log_error(NGX_LOG_ERR, worker_ctx->log, 0, "ngx media worker mss response code:[%d] error,msg:[%V].",
                                             status_code,&worker_ctx->mss_req_msg);
    return;


}


static void
ngx_media_worker_mss_send_body(void *request, ngx_http_request_t *hcr)
{
    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)request;

    ngx_chain_t out;
    ngx_buf_t   b;
    out.buf     = &b;
    out.next    = NULL;

    b->pos      = worker_ctx->mss_req_msg.data;
    b->last     = worker_ctx->mss_req_msg.data + worker_ctx->mss_req_msg.len;

    b->memory   = 1;
    b->last_buf = 1;

    if (NGX_OK != ngx_http_client_write_body(hcr,&out)) {
        ngx_log_debug0(NGX_LOG_ERR, worker_ctx->log, 0,
                          "ngx_media_worker_mss_send_body,send the msg body fail.");
        worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_BREAK;
    }
    return;
}

static ngx_int_t
ngx_media_worker_mss_create_req_msg(ngx_worker_mss_ctx_t *ctx)
{
    ngx_uint_t lens  = 0;
    ngx_int_t  ret   = NGX_ERROR;
    MSS_MEDIA_TYE enMediaType = MSS_MEDIA_TYE_LIVE;

    if(ngx_strncmp(ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_LIVE,
                                ngx_strlen(MSS_WORK_MEDIA_TYPE_LIVE)) == 0) {
        enMediaType = MSS_MEDIA_TYE_LIVE;
    }
    else if(ngx_strncmp(ctx->mss_arg.mss_type.data,MSS_WORK_MEDIA_TYPE_RECORD,
                                ngx_strlen(MSS_WORK_MEDIA_TYPE_RECORD)) == 0) {
        enMediaType = MSS_MEDIA_TYE_RECORD;
    }
    else {
        return NGX_ERROR;
    }

    cJSON* root = cJSON_CreateObject();
    if(NULL == root) {
        return NGX_ERROR;
    }
    do {
        if(MSS_MEDIA_TYE_LIVE == enMediaType) {
            cJSON_AddItemToObject(root, "cameraId", cJSON_CreateString(ctx->mss_arg.mss_cameraid.data));
            cJSON_AddItemToObject(root, "streamType", cJSON_CreateString(ctx->mss_arg.mss_streamtype.data));
            cJSON_AddItemToObject(root, "urlType", cJSON_CreateString("1"));
        }
        else if(MSS_MEDIA_TYE_RECORD == enMediaType) {
            cJSON_AddItemToObject(root, "cameraId", cJSON_CreateString(ctx->mss_arg.mss_cameraid.data));
            cJSON_AddItemToObject(root, "streamType", cJSON_CreateString(ctx->mss_arg.mss_streamtype.data));
            cJSON_AddItemToObject(root, "urlType", cJSON_CreateString("1"));
            cJSON_AddItemToObject(root, "vodType", cJSON_CreateString("download"));
            cJSON* vodInfo = cJSON_CreateObject();
            cJSON_AddItemToObject(vodInfo, "contentId", cJSON_CreateString(ctx->mss_arg.mss_contentid.data));
            cJSON_AddItemToObject(vodInfo, "cameraId", cJSON_CreateString(ctx->mss_arg.mss_cameraid.data));
            cJSON_AddItemToObject(vodInfo, "beginTime", cJSON_CreateString(ctx->mss_arg.mss_starttime.data));
            cJSON_AddItemToObject(vodInfo, "endTime", cJSON_CreateString(ctx->mss_arg.mss_endtime.data));
            cJSON_AddItemToObject(vodInfo, "nvrCode", cJSON_CreateString(ctx->mss_arg.mss_nvrcode.data));
        }
        char* msg = cJSON_PrintUnformatted(root);
        lens = ngx_strlen(msg);
        ctx->mss_req_msg.data = ngx_pcalloc(ctx->pool,lens + 1);
        ctx->mss_req_msg.len = lens;
        ngx_memcpy(ctx->mss_req_msg.data, msg,lens);
        free(msg);
        ret   = NGX_OK;
    }while(false);

    cJSON_Delete(root);
    return ret;
}

static ngx_keyval_t*
ngx_media_worker_mss_create_header(ngx_worker_mss_ctx_t *ctx)
{
    ngx_keyval_t *headers = NULL;
    char   Digest[HTTP_DIGEST_LENS_MAX] = {0};

    /* Authorization,Content-Type,Content-length */
    ngx_uint_t len = sizeof(ngx_keyval_t)*(NGX_MEDIA_WORKER_MSS_HTTP_HEADER_MAX + 1);

    headers = (ngx_keyval_t *)ngx_palloc(ctx->pool,len);

    /* Content-Type */
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_TYPE].key.data   = "Content-Type";
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_TYPE].key.len    = ngx_strlen("Content-Type");
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_TYPE].value.data = "application/json";
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_TYPE].value.len  = ngx_strlen("application/json");

    /* Content-length */
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_LENGHT].key.data   = "Content-length";
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_LENGHT].key.len    = ngx_strlen("Content-length");

    u_char* p = ngx_palloc(ctx->pool,10);
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_LENGHT].value.data = p;
    u_char* last = ngx_snprintf(p, 10,"%d",ctx->mss_req_msg.len);
    *last = '\0';
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_LENGHT].value.len  = ngx_strlen("p");

    /* Authorization */
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_AUTH].key.data   = "Authorization";
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_AUTH].key.len    = ngx_strlen("Authorization");

    p = ngx_palloc(ctx->pool,1024);
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_AUTH].value.data = p;

    ngx_digest_attr_value_t value;
    value.string = (char*)ctx->mss_arg.mss_name.data;
    ngx_digest_set_attr(&ctx->mss_digest, D_ATTR_USERNAME,value);
    value.string = (char*)ctx->mss_arg.mss_passwd.data;
    ngx_digest_set_attr(&ctx->mss_digest, D_ATTR_PASSWORD,value);
    value.string = (char*)ctx->mss_arg.mss_addr.data;
    ngx_digest_set_attr(&ctx->mss_digest, D_ATTR_URI,value);
    value.number = DIGEST_METHOD_POST;
    ngx_digest_set_attr(&ctx->mss_digest, D_ATTR_METHOD, value);

    if (-1 == ngx_digest_client_generate_header(&ctx->mss_digest, p,1024)) {
         (void)ngx_cpystrn(p,"Digest username=test,realm=test, nonce =0001,"
                       "uri=/api/alarm/list,response=c76a6680f0f1c8e26723d81b44977df0,"
                       "cnonce=00000001,opaque=000001,qop=auth,nc=00000001",
                       1024);
    }

    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_AUTH].value.len  = ngx_strlen("p");

    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_MAX].key.data   = NULL;
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_MAX].key.len    = 0;
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_MAX].value.data = NULL;
    headers[NGX_MEDIA_WORKER_MSS_HTTP_HEADER_MAX].value.len  = 0;


    return headers;
}


static ngx_int_t
ngx_media_worker_mss_url_request(ngx_worker_mss_ctx_t *ctx)
{
    ngx_keyval_t *headers = NULL;
    if(NULL != ctx->mss_req) {
        ngx_http_client_finalize_request(ctx->mss_req,1);
        ctx->mss_req = NULL;
    }
    /* create the message body */
    if(0 == ctx->mss_req_msg.len) {
        if(NGX_OK != ngx_media_worker_mss_create_req_msg) {
            return NGX_ERROR;
        }
    }
    /* create the auth info */
    headers = ngx_media_worker_mss_create_header(ctx);

    ctx->mss_req = ngx_http_client_create_request(ctx->mss_arg.mss_addr,NGX_HTTP_CLIENT_POST,
                                                  NGX_HTTP_CLIENT_VERSION_11,headers,
                                                  ctx->log,ngx_media_worker_mss_recv_body,ngx_media_worker_mss_send_body);
    if(NULL == ctx->mss_req) {
        return NGX_ERROR;
    }
    ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_REQ_URL;
    return ngx_http_client_send(ctx->mss_req,NULL,ctx,ctx->log);
}

static ngx_int_t
ngx_media_worker_mss_init(ngx_media_worker_ctx_t* ctx,WK_WATCH watch)
{
    ngx_uint_t lens = sizeof(ngx_worker_mss_ctx_t);
    ngx_worker_mss_ctx_t *worker_ctx = ngx_pcalloc(ctx->pool,lens);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ctx->log, 0,
                          "ngx_media_worker_mss_init begin");

    if(NULL == worker_ctx) {
        ngx_log_debug0(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_mss_init allocate mss ctx fail.");
        return NGX_ERROR;
    }

    ctx->priv_data_size    = lens;
    ctx->priv_data         = worker_ctx;

    worker_ctx->pool       = ctx->pool;
    worker_ctx->log        = ctx->log;
    worker_ctx->watcher    = watch;
    worker_ctx->wk_ctx     = ctx;

    ngx_memzero(&worker_ctx->timer, sizeof(ngx_event_t));
    ngx_memzero(&worker_ctx->mss_arg, sizeof(ngx_worker_mss_arg_t));
    worker_ctx->mss_req    = NULL;
    ngx_str_null(&worker_ctx->mss_req_msg);
    ngx_str_null(&worker_ctx->mss_resp_msg);

    worker_ctx->run_handle = NULL;
    ngx_memzero(&worker_ctx->worker_stat, sizeof(mk_task_stat_info_t));
    worker_ctx->mk_nparamcount = 0;
    worker_ctx->mk_paramlist   = NULL;

    /* parser the mss args */
    if(NGX_OK != ngx_media_worker_mss_parser_args(ctx)) {
        ngx_log_debug0(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_worker_mss_init,parser the mss args fail.");
        return NGX_ERROR
    }
    if (-1 == ngx_digest_init(&ctx->mss_digest,0)) {
        return NGX_ERROR;
    }

    worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_INIT;

    return NGX_OK;
}

static ngx_int_t
ngx_media_worker_mss_release(ngx_media_worker_ctx_t* ctx)
{
    if(ctx->priv_data_size != sizeof(ngx_worker_mss_ctx_t)) {
        return NGX_OK;
    }

    if(NULL == ctx->priv_data) {
        return NGX_OK;
    }
    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)ctx->priv_data;
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
ngx_media_worker_mss_start(ngx_media_worker_ctx_t* ctx)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mss_start worker:[%V] begin.",&ctx->wokerid);
    if(ctx->priv_data_size != sizeof(ngx_worker_mss_ctx_t)) {
        return NGX_ERROR;
    }

    if(NULL == ctx->priv_data) {
        return NGX_ERROR;
    }

    ngx_worker_mss_ctx_t *worker_ctx = (ngx_worker_mss_ctx_t *)ctx->priv_data;

    /* start the first http request  from mss */
    if(NGX_OK != ngx_media_worker_mss_url_request(worker_ctx)) {
        ngx_log_error(NGX_LOG_WARN, ctx->log, 0,
                          "ngx_media_worker_mss_start worker:[%V] send mss request fail.",&ctx->wokerid);
        return NGX_ERROR;
    }
    ngx_memzero(&worker_ctx->timer, sizeof(ngx_event_t));

    worker_ctx->timer.handler = ngx_media_worker_mss_timer;
    worker_ctx->timer.log     = ctx->log;
    worker_ctx->timer.data    = worker_ctx;

    ngx_add_timer(&worker_ctx->timer,NGX_HTTP_VIDEO_MSS_TIME);

    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_worker_mss_start worker:[%V] end.",&ctx->wokerid);

    return NGX_OK;
}


static ngx_int_t
ngx_media_worker_mss_stop(ngx_media_worker_ctx_t* ctx)
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
    worker_ctx->status = NGX_MEDIA_WOKER_MSS_STATUS_END;
    return NGX_OK;
}


/* mss media worker */
ngx_media_worker_t ngx_video_mss_worker = {
    .type             = ngx_media_worker_mss,
    .init_worker      = ngx_media_worker_mss_init,
    .release_worker   = ngx_media_worker_mss_release,
    .start_worker     = ngx_media_worker_mss_start,
    .stop_worker      = ngx_media_worker_mss_stop,
};



