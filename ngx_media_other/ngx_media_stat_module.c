/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_stat_module.c
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
#include "ngx_media_stat_module.h"
#include "ngx_media_include.h"
#include "ngx_media_sys_stat.h"
#include <zookeeper/zookeeper.h>
#include <cjson/cJSON.h>
#include "ngx_media_worker.h"
#include "ngx_media_license_module.h"

extern ngx_media_task_ctx_t video_task_ctx;

#define NGX_HTTP_STAT_HTML_ROW_MAX  4096

typedef struct {
    ngx_str_t                       manage_passwd;
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
} ngx_media_stat_main_conf_t;


/*************************video task init *****************************/
static char*      ngx_media_stat_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void*      ngx_media_stat_create_main_conf(ngx_conf_t *cf);

/*************************video task request *****************************/
static ngx_int_t  ngx_media_stat_list_task_html(ngx_http_request_t *r);
static ngx_int_t  ngx_media_stat_manage_task(ngx_http_request_t *r);
static ngx_int_t  ngx_media_stat_task_detail(ngx_http_request_t *r);
static ngx_int_t  ngx_media_stat_error_html(ngx_http_request_t *r,ngx_int_t code);
static ngx_int_t  ngx_media_stat_password(ngx_http_request_t *r,ngx_str_t* password);
static u_char*    ngx_media_stat_time2string(u_char *buf, time_t t);
static ngx_uint_t ngx_media_stat_worker_count(ngx_media_task_t *task);
static ngx_int_t  ngx_media_stat_stop_task(ngx_http_request_t *r,ngx_str_t* taskid);
static ngx_int_t  ngx_media_stat_stop_worker(ngx_http_request_t *r,ngx_str_t* taskid,ngx_str_t* workerid);




static ngx_command_t  ngx_media_stat_commands[] = {

    { ngx_string(NGX_HTTP_VIDEO_STAT),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_media_stat_init,
      0,
      0,
      NULL },

    { ngx_string(NGX_HTTP_STAT_PASSWD),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_stat_main_conf_t, manage_passwd),
      NULL },
      ngx_null_command
};


static ngx_http_module_t  ngx_media_stat_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    ngx_media_stat_create_main_conf,   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    NULL,                                   /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_stat_module = {
    NGX_MODULE_V1,
    &ngx_media_stat_module_ctx,       /* module context */
    ngx_media_stat_commands,          /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL                            ,      /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_media_stat_handler(ngx_http_request_t *r)
{

    ngx_int_t rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    ngx_str_t    value;

    if (r->args.len) {
        if (ngx_http_arg(r, (u_char *) TRANS_REQ_ARG_MANAGE, ngx_strlen(TRANS_REQ_ARG_MANAGE), &value) == NGX_OK) {

            if(ngx_strncmp(value.data,TRANS_REQ_ARG_STOP,ngx_strlen(TRANS_REQ_ARG_STOP)) == 0) {
                return ngx_media_stat_manage_task(r);
            }
            else if(ngx_strncmp(value.data,TRANS_REQ_ARG_DETAIL,ngx_strlen(TRANS_REQ_ARG_DETAIL)) == 0) {
                return ngx_media_stat_task_detail(r);
            }
            else {
                return ngx_media_stat_list_task_html(r);
            }
        }
    }
    return ngx_media_stat_list_task_html(r);
}


static void *
ngx_media_stat_create_main_conf(ngx_conf_t *cf)
{
    ngx_media_stat_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_media_stat_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    ngx_str_null(&conf->manage_passwd);
    conf->log           = cf->log;
    conf->pool          = cf->pool;
    return conf;
}

static char*
ngx_media_stat_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_stat_handler;


    return NGX_CONF_OK;
}

static u_char *
ngx_media_stat_time2string(u_char *buf, time_t t)
{
    ngx_tm_t  tm;

    ngx_localtime(t, &tm);

    return ngx_sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
                       tm.tm_year,
                       tm.tm_mon,
                       tm.tm_mday,
                       tm.tm_hour,
                       tm.tm_min,
                       tm.tm_sec);

}
static ngx_uint_t
ngx_media_stat_worker_count(ngx_media_task_t *task)
{
    ngx_uint_t                     count  = 0;
    ngx_list_part_t               *part   = NULL;

    if(NULL == task) {
        return 0;
    }

    part  = &(task->workers->part);
    while (part)
    {
        count += part->nelts;
        part = part->next;
    }

    return count;
}


static ngx_int_t
ngx_media_stat_list_task_html(ngx_http_request_t *r)
{
    int                             buffersize  = 0;
    ngx_uint_t                      i           = 0;
    ngx_int_t                       rc          = NGX_OK;
    u_char                         *pbuff       = NULL;
    ngx_media_worker_ctx_t    *worker      = NULL;
    ngx_media_worker_ctx_t    *array       = NULL;
    ngx_list_part_t                *part        = NULL;
    ngx_media_task_t          *task        = NULL;
    ngx_str_t                       password;
    u_char                         *status      = NULL;
    ngx_uint_t                      wk_count    = 0;


    ngx_chain_t                     out_head;
    ngx_chain_t                     out_foot;
    ngx_chain_t                    *out_body;
    ngx_chain_t                    *out_pre;
    ngx_buf_t                      *b;
    u_char                          buf[128];
    ngx_memzero(&buf, 128);

    ngx_str_null(&password);


    if (ngx_http_arg(r, (u_char *) TRANS_REQ_ARG_PASSWD, ngx_strlen(TRANS_REQ_ARG_PASSWD), &password) == NGX_OK) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "have the password!");
    }



    /*head */
    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create head buf fail!");
        return NGX_HTTP_NOT_FOUND;
    }
    pbuff = ngx_pcalloc(r->pool, NGX_HTTP_STAT_HTML_ROW_MAX);
    if (pbuff == NULL) {
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create head data fail!");
        return NGX_HTTP_NOT_FOUND;
    }

    b->last = ngx_snprintf(pbuff,NGX_HTTP_STAT_HTML_ROW_MAX,
                      "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=GBK\">"
                           "<title>AllMedia</title>"
	                       "</head><body>alltasklist[%d]<br><table border=\"1\"><tbody>"
                           "<tr><td >Task</td><td>State</td><td>stop</td><td>detail</td><td>worker</td><td>master</td><td>State</td><td>update</td><td>stop</td></tr>",
                           video_task_ctx.task_count);
    b->pos = pbuff;
    b->memory = 1;
    out_head.buf  = b;
    out_head.next = NULL;
    out_pre = &out_head;
    buffersize += (b->last - b->pos);


    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
         ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "video task lock task context fail!");
        return NGX_HTTP_NOT_FOUND;
    }
    task = video_task_ctx.task_head;
    while(NULL != task) {
        /* task body */
        out_body = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
        if (out_body == NULL) {
            ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create body chain buf fail!");
            task = task->next_task;
            continue;
        }
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create body buf fail!");
            task = task->next_task;
            continue;
        }
        pbuff = ngx_pcalloc(r->pool, NGX_HTTP_STAT_HTML_ROW_MAX);
        if (pbuff == NULL) {
            ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create body data fail!");
            task = task->next_task;
            continue;
        }
        if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
            ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task lock task fail!");
            task = task->next_task;
            continue;
        }


        if(task->status == ngx_media_worker_status_start) {
            status = (u_char*)TASK_COMMAND_START;
        }
        else if(task->status == ngx_media_worker_status_running) {
            status = (u_char*)TASK_COMMAND_RUNNING;
        }
        else if(task->status == ngx_media_worker_status_stop) {
            status = (u_char*)TASK_COMMAND_STOP;
        }
        else {
            status = (u_char*)TASK_COMMAND_INIT;
        }

        wk_count = ngx_media_stat_worker_count(task);

        if(0 < password.len) {
            b->last = ngx_snprintf(pbuff,NGX_HTTP_STAT_HTML_ROW_MAX,
                      "<tr><th rowspan=\"%d\">%V</th><th rowspan=\"%d\">%s</th>"
                      "<th rowspan=\"%d\"><a href=\"%V?manager=stop&task=%V&%s=%V\">stop</a></th>"
                      "<th rowspan=\"%d\"><a href=\"%V?manager=detail&task=%V\">detail</a></th>",
                      wk_count,
                      &task->task_id,
                      wk_count,
                      status,
                      wk_count,
                      &r->uri,
                      &task->task_id,
                      TRANS_REQ_ARG_PASSWD,
                      &password,
                      wk_count,
                      &r->uri,
                      &task->task_id);
        }
        else {
            b->last = ngx_snprintf(pbuff,NGX_HTTP_STAT_HTML_ROW_MAX,
                      "<tr><th rowspan=\"%d\">%V</th><th rowspan=\"%d\">%s</th>"
                      "<th rowspan=\"%d\"><a href=\"%V?manager=stop&task=%V\">stop</a></th>"
                      "<th rowspan=\"%d\"><a href=\"%V?manager=detail&task=%V\">detail</a></th>",
                      wk_count,
                      &task->task_id,
                      wk_count,
                      status,
                      wk_count,
                      &r->uri,
                      &task->task_id,
                      wk_count,
                      &r->uri,
                      &task->task_id);
        }

        part  = &(task->workers->part);
        while (part)
        {
            array = (ngx_media_worker_ctx_t*)(part->elts);
            ngx_uint_t loop = 0;
            for (; loop < part->nelts; loop++)
            {
                worker = &array[loop];

                ngx_media_stat_time2string(buf,worker->updatetime);

                if(0 != i){
                   b->last = ngx_snprintf(b->last,NGX_HTTP_STAT_HTML_ROW_MAX,
                          "<tr>");
                }

                if(worker->status == ngx_media_worker_status_start) {
                    status = (u_char*)TASK_COMMAND_START;
                }
                else if(worker->status == ngx_media_worker_status_running) {
                    status = (u_char*)TASK_COMMAND_RUNNING;
                }
                else if(worker->status == ngx_media_worker_status_stop) {
                    status = (u_char*)TASK_COMMAND_STOP;
                }
                else {
                    status = (u_char*)TASK_COMMAND_INIT;
                }

                if(0 < password.len) {
                    b->last = ngx_snprintf(b->last,NGX_HTTP_STAT_HTML_ROW_MAX,
                          "<td>%V</td><td>%d</td><td>%s</td><td>%s</td><td ><a href=\"%V?manager=stop&task=%V&worker=%V&%s=%V\">stop</a></td></tr>",
                          &worker->wokerid,
                          worker->master,
                          status,
                          buf,
                          &r->uri,
                          &task->task_id,
                          &worker->wokerid,
                          TRANS_REQ_ARG_PASSWD,
                          &password);
                }
                else {
                    b->last = ngx_snprintf(b->last,NGX_HTTP_STAT_HTML_ROW_MAX,
                          "<td>%V</td><td>%d</td><td>%s</td><td>%s</td><td ><a href=\"%V?manager=stop&task=%V&worker=%V\">stop</a></td></tr>",
                          &worker->wokerid,
                          worker->master,
                          status,
                          buf,
                          &r->uri,
                          &task->task_id,
                          &worker->wokerid);
                }
            }

            part = part->next;

        }
        ngx_thread_mutex_unlock(&task->task_mtx, task->log);

        b->pos = pbuff;
        b->memory = 1;
        out_body->buf  = b;
        out_body->next = NULL;
        out_pre->next  = out_body;
        out_pre = out_body;
        buffersize += (b->last - b->pos);
        task = task->next_task;
    }

    if (ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
         ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "video task unlock task context fail!");
    }

    /*foot */
    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create foot buf fail!");
        return NGX_HTTP_NOT_FOUND;
    }
    pbuff = ngx_pcalloc(r->pool, NGX_HTTP_STAT_HTML_ROW_MAX);
    if (pbuff == NULL) {
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create foot data fail!");
        return NGX_HTTP_NOT_FOUND;
    }

    ngx_uint_t lice_task = ngx_media_license_task_count();
    ngx_uint_t lice_rtmp = ngx_media_license_rtmp_channle();
    ngx_uint_t lice_rtsp = ngx_media_license_rtsp_channle();
    ngx_uint_t lice_hls  = ngx_media_license_hls_channle();
    b->last = ngx_snprintf(pbuff,NGX_HTTP_STAT_HTML_ROW_MAX,
                      "</tbody></table>"
                      "<br>---connection:max:%d/free:%d---"
                      "<br>---license:task:[%d],rtmp:[%d],rtsp:[%d],hls:[%d]---"
                      "<br>---allmedia:%s %s---</body></html>",
                      ngx_cycle->connection_n,ngx_cycle->free_connection_n,
                      lice_task,lice_rtmp,lice_rtsp,lice_hls,
                      __DATE__,__TIME__);
    b->pos        = pbuff;
    b->memory     = 1;
    b->last_buf   = 1;
    out_foot.buf  = b;
    out_foot.next = NULL;
    out_pre->next = &out_foot;
    buffersize += (b->last - b->pos);

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char*)"text/html";
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = buffersize;
    r->keepalive   = 0;
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out_head);
}

static ngx_int_t
ngx_media_stat_manage_task(ngx_http_request_t *r)
{
    ngx_str_t    taskid;
    ngx_str_t    wokerid;
    ngx_str_t    password;
    ngx_int_t    rc = NGX_ERROR;
     /* discard request body, since we don't need it here */
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }
    rc = NGX_ERROR;

    if (ngx_http_arg(r, (u_char *) TRANS_REQ_ARG_TASK, ngx_strlen(TRANS_REQ_ARG_TASK), &taskid) != NGX_OK) {
        return ngx_media_stat_error_html(r,rc);
    }
    if (ngx_http_arg(r, (u_char *) TRANS_REQ_ARG_PASSWD, ngx_strlen(TRANS_REQ_ARG_PASSWD), &password) != NGX_OK) {
        return ngx_media_stat_error_html(r,rc);
    }
    if (ngx_http_arg(r, (u_char *) TRANS_REQ_ARG_WORKER, ngx_strlen(TRANS_REQ_ARG_WORKER), &wokerid) != NGX_OK) {
        wokerid.len = 0;
    }

    rc = ngx_media_stat_password(r,&password);
    if(NGX_OK != rc){
        return ngx_media_stat_error_html(r,rc);
    }

    if(0 == wokerid.len) {
        rc = ngx_media_stat_stop_task(r,&taskid);
    }
    else{
        rc = ngx_media_stat_stop_worker(r,&taskid,&wokerid);
    }
    return ngx_media_stat_error_html(r,rc);
}

static ngx_int_t
ngx_media_stat_stop_task(ngx_http_request_t *r,ngx_str_t* taskid)
{
    ngx_media_task_t         *task   = NULL;
    ngx_media_task_t         *find   = NULL;
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t               *part   = NULL;

    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return NGX_ERROR;
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
        return NGX_ERROR;
    }
    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);
        return NGX_ERROR;
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

                if(ngx_media_worker_status_running == worker->status) {
                    if(NGX_OK != worker->worker->stop_worker(worker)) {
                        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                                     "ngx http video task trigger start task worker fail.");
                    }
                }
                ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
            }
            break;
        }
        part = part->next;
    }

    ngx_thread_mutex_unlock(&task->task_mtx, task->log);

    ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);

    return NGX_OK;
}


static ngx_int_t
ngx_media_stat_stop_worker(ngx_http_request_t *r,ngx_str_t* taskid,ngx_str_t* workerid)
{

    ngx_media_task_t         *task   = NULL;
    ngx_media_task_t         *find   = NULL;
    ngx_media_worker_ctx_t   *worker = NULL;
    ngx_media_worker_ctx_t   *array  = NULL;
    ngx_list_part_t               *part   = NULL;
    ngx_int_t                      stop   = 0;

    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return NGX_ERROR;
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
        return NGX_ERROR;
    }
    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);
        return NGX_ERROR;
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
            stop = 1;
            if(ngx_thread_mutex_lock(&worker->work_mtx, worker->log) == NGX_OK) {

                if(ngx_media_worker_status_running == worker->status) {
                    if(NGX_OK != worker->worker->stop_worker(worker)) {
                        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                                     "ngx http video task trigger start task worker fail.");
                    }
                }
                ngx_thread_mutex_unlock(&worker->work_mtx, worker->log);
            }
            break;
        }
        if(stop) {
            break;
        }
        part = part->next;
    }

    ngx_thread_mutex_unlock(&task->task_mtx, task->log);

    ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);

    if(!stop) {
        return NGX_ERROR;
    }

    return NGX_OK;
}



static ngx_int_t
ngx_media_stat_task_detail(ngx_http_request_t *r)
{

    u_char                         *pbuff       = NULL;
    ngx_chain_t                     out;
    ngx_buf_t                      *b;
    ngx_str_t                       taskid;
    ngx_int_t                       rc = NGX_ERROR;

    ngx_media_task_t         *task   = NULL;
    ngx_media_task_t         *find   = NULL;



    if (ngx_http_arg(r, (u_char *) TRANS_REQ_ARG_TASK, ngx_strlen(TRANS_REQ_ARG_TASK), &taskid) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "detail have no task info!");
        return ngx_media_stat_error_html(r,rc);
    }



    if (ngx_thread_mutex_lock(&video_task_ctx.task_thread_mtx, video_task_ctx.log) != NGX_OK) {
        return ngx_media_stat_error_html(r,rc);
    }

    find = video_task_ctx.task_head;

    while(NULL != find) {
        if(ngx_strncmp(taskid.data,find->task_id.data,find->task_id.len) == 0) {
            task = find;
            break;
        }
        find = find->next_task;
    }

    if(NULL == task) {
        ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);
        return ngx_media_stat_error_html(r,rc);
    }
    if (ngx_thread_mutex_lock(&task->task_mtx, task->log) != NGX_OK) {
        ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);
        return ngx_media_stat_error_html(r,rc);
    }


    r->headers_out.content_type.len = sizeof("application/xml") - 1;
    r->headers_out.content_type.data = (u_char*)"application/xml";


    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create buf fail!");
        return NGX_HTTP_NOT_FOUND;
    }
    pbuff = ngx_pcalloc(r->pool, task->xml.len +1);
    if (pbuff == NULL) {
        ngx_log_error(NGX_LOG_EMERG, task->log, 0, "video task create data fail!");
        return NGX_HTTP_NOT_FOUND;
    }

    b->last = ngx_copy(pbuff,task->xml.data,task->xml.len);

    b->pos = pbuff;
    b->last = pbuff + task->xml.len;

    b->memory = 1;
    b->last_buf = 1;

    out.buf  = b;
    out.next = NULL;

    ngx_thread_mutex_unlock(&task->task_mtx, task->log);
    ngx_thread_mutex_unlock(&video_task_ctx.task_thread_mtx, video_task_ctx.log);

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = task->xml.len;
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}

static ngx_int_t
ngx_media_stat_error_html(ngx_http_request_t *r,ngx_int_t code)
{
    ngx_int_t                       rc          = NGX_OK;
    ngx_chain_t                     out;
    ngx_buf_t                      *b;

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char*)"text/html";


    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "video task create error buf fail!");
        return NGX_HTTP_NOT_FOUND;
    }

    ngx_uint_t len = ngx_strlen(TRANS_ERROR_HTML_MSG);
    b->pos = (u_char*)TRANS_ERROR_HTML_MSG;
    if(NGX_OK == code){
        len = ngx_strlen(TRANS_SUCCESS_HTML_MSG);
        b->pos = (u_char*)TRANS_SUCCESS_HTML_MSG;
    }


    b->last = b->pos + len;

    b->memory = 1;
    b->last_buf = 1;

    out.buf  = b;
    out.next = NULL;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = len;
    r->keepalive   = 0;
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}
static ngx_int_t
ngx_media_stat_password(ngx_http_request_t *r,ngx_str_t* password)
{
    ngx_media_stat_main_conf_t* trans_conf;
    trans_conf = ngx_http_get_module_main_conf(r, ngx_media_stat_module);

    if(NULL == trans_conf) {
        return NGX_ERROR;
    }

    if(NULL == password) {
        return NGX_ERROR;
    }

    if((0 == password->len)
      ||(NULL == password->data)){
        return NGX_ERROR;
    }

    if((0 == trans_conf->manage_passwd.len)
      ||(NULL == trans_conf->manage_passwd.data)){
        return NGX_ERROR;
    }

    if(ngx_memcmp(trans_conf->manage_passwd.data,
                  password->data,trans_conf->manage_passwd.len) == 0){
        return NGX_OK;
    }

    return NGX_ERROR;
}






