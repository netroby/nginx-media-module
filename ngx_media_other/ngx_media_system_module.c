/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_system_module.c
 Version    : V 1.0.0
 Date       : 2016-04-28
 Author     : hexin H.kernel
 Modify     :
            1.2016-04-28: create
            2.2016-04-29: add file manager.
            3.2017-07-20: add disk manager
******************************************************************************/


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>
#include "ngx_media_system_module.h"
#include "ngx_media_task_module.h"
#include "ngx_media_license_module.h"
#include "libMediaKenerl.h"
#include "mk_def.h"
#include "ngx_media_include.h"
#include "ngx_media_sys_stat.h"
#include <zookeeper/zookeeper.h>
#include <cjson/cJSON.h>



typedef struct {
    ngx_str_t                       sch_zk_address;
    char                           *str_zk_address;
    ngx_str_t                       sch_zk_server_id;
    ngx_uint_t                      sch_server_flags;
    char                           *str_zk_trans_path;
    char                           *str_zk_access_path;
    char                           *str_zk_stream_path;
    ngx_msec_t                      sch_zk_update;
    ngx_keyval_t                    sch_signal_ip;
    ngx_keyval_t                    sch_service_ip;
    ngx_array_t                    *sch_disk_vpath;
}ngx_media_system_conf_t;


typedef struct {
    zhandle_t                      *sch_zk_handle;
    ngx_media_system_conf_t         sys_conf;
    ngx_event_t                     sch_zk_timer;
    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
} ngx_media_system_main_conf_t;

static ngx_media_system_conf_t* g_VideoSysConf = NULL;




static char*     ngx_media_system_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char*     ngx_media_system_signal_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char*     ngx_media_system_service_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_media_system_init_process(ngx_cycle_t *cycle);
static void      ngx_media_system_exit_process(ngx_cycle_t *cycle);
static void*     ngx_media_system_create_main_conf(ngx_conf_t *cf);

/*************************zookeeper schedule****************************/
static void      ngx_media_system_zk_root_exsit_completion_t(int rc, const struct Stat *stat,const void *data);
static void      ngx_media_system_zk_root_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_media_system_zk_transcode_exsit_completion_t(int rc, const struct Stat *stat,const void *data);
static void      ngx_media_system_zk_transcode_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_media_system_zk_transcode_node_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_media_system_zk_transcode_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data);
static void      ngx_media_system_zk_access_exsit_completion_t(int rc, const struct Stat *stat,const void *data);
static void      ngx_media_system_zk_access_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_media_system_zk_access_node_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_media_system_zk_access_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data);
static void      ngx_media_system_zk_stream_exsit_completion_t(int rc, const struct Stat *stat,const void *data);
static void      ngx_media_system_zk_stream_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_media_system_zk_stream_node_create_completion_t(int rc, const char *value, const void *data);
static void      ngx_media_system_zk_stream_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data);
static void      ngx_media_system_zk_sync_system_info(ngx_media_system_main_conf_t* conf, const char *value, int value_len,char* zk_path);
static void      ngx_media_system_zk_watcher(zhandle_t *zh, int type,int state, const char *path,void *watcherCtx);
static void      ngx_media_system_zk_init_timer(ngx_media_system_main_conf_t* conf);
static void      ngx_media_system_zk_check_timeout(ngx_event_t *ev);
static void      ngx_media_system_zk_invoke_stat(ngx_media_system_main_conf_t* conf,ngx_uint_t flag);
static void      ngx_media_system_zk_stat_completion_t(int rc, const struct Stat *stat,const void *data);




static ngx_int_t ngx_media_system_disk_stat(ngx_http_request_t *r,ngx_chain_t* out);
static ngx_int_t ngx_media_system_disk_list_file(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_system_disk_list_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_system_disk_list_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_system_disk_list(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out);
static ngx_int_t ngx_media_system_disk_delete_file(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_system_disk_delete_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_system_disk_delete_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path);
static ngx_int_t ngx_media_system_disk_delete(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out);
static ngx_int_t ngx_media_system_disk_req(ngx_http_request_t *r,xmlDocPtr doc,ngx_chain_t* out);
static ngx_int_t ngx_media_system_deal_xml_req(ngx_http_request_t *r,const char* req_xml,ngx_chain_t* out);
static void      ngx_media_system_recv_request(ngx_http_request_t *r);


static ngx_conf_bitmask_t  ngx_media_zk_type_mask[] = {
    { ngx_string("all"),                NGX_ALLMEDIA_TYPE_ALL       },
    { ngx_string("transcode"),          NGX_ALLMEDIA_TYPE_TRANSCODE },
    { ngx_string("access"),             NGX_ALLMEDIA_TYPE_ACCESS    },
    { ngx_string("stream"),             NGX_ALLMEDIA_TYPE_STREAM    },
    { ngx_null_string,                  0                           }
};



static ngx_command_t  ngx_media_system_commands[] = {

    { ngx_string(NGX_HTTP_SYS_MANAGE),
      NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      ngx_media_system_init,
      0,
      0,
      NULL },

    { ngx_string(NGX_HTTP_SCH_ZK_ADDR),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_system_main_conf_t, sys_conf.sch_zk_address),
      NULL },

    { ngx_string(NGX_HTTP_SCH_ZK_SERVERID),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_system_main_conf_t, sys_conf.sch_zk_server_id),
      NULL },

    { ngx_string(NGX_HTTP_SCH_ZK_SERTYPE),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_system_main_conf_t, sys_conf.sch_server_flags),
      ngx_media_zk_type_mask },

    { ngx_string(NGX_HTTP_SCH_ZK_UPDATE),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_system_main_conf_t, sys_conf.sch_zk_update),
      NULL },

    { ngx_string(NGX_HTTP_SCH_ZK_SIGIP),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_media_system_signal_address,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_system_main_conf_t, sys_conf.sch_signal_ip),
      NULL },

    { ngx_string(NGX_HTTP_SCH_ZK_SERIP),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_media_system_service_address,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_system_main_conf_t, sys_conf.sch_service_ip),
      NULL },

    { ngx_string(NGX_HTTP_SCH_ZK_VPATH),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_keyval_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_media_system_main_conf_t, sys_conf.sch_disk_vpath),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_media_system_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    ngx_media_system_create_main_conf,      /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    NULL,                                   /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_system_module = {
    NGX_MODULE_V1,
    &ngx_media_system_module_ctx,             /* module context */
    ngx_media_system_commands,                /* module directives */
    NGX_HTTP_MODULE,                          /* module type */
    NULL,                                     /* init master */
    NULL,                                     /* init module */
    ngx_media_system_init_process,            /* init process */
    NULL,                                     /* init thread */
    NULL,                                     /* exit thread */
    ngx_media_system_exit_process,            /* exit process */
    NULL,                                     /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_media_system_handler(ngx_http_request_t *r)
{
    ngx_int_t                      rc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                          "ngx http vido handle manage request.");


    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD|NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx http vido manage request method is invalid.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->uri.data[r->uri.len - 1] == '/') {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx http vido manage request uri is invalid.");
        return NGX_DECLINED;
    }


    /*  remove the args from the uri */
    if (r->args_start)
    {
        r->uri.len = r->args_start - 1 - r->uri_start;
        r->uri.data[r->uri.len] ='\0';
    }

    /* deal the http request with xml content */
    r->request_body_in_single_buf = 1;
    rc = ngx_http_read_client_request_body(r,ngx_media_system_recv_request);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }
    /* return the NGX_DONE and send response by the ngx_media_system_recv_request */
    return NGX_DONE;
}

static void *
ngx_media_system_create_main_conf(ngx_conf_t *cf)
{
    ngx_media_system_main_conf_t* mconf = NULL;
    g_VideoSysConf = NULL;

    mconf = ngx_pcalloc(cf->pool, sizeof(ngx_media_system_main_conf_t));
    if (mconf == NULL)
    {
        return NULL;
    }


    ngx_str_null(&mconf->sys_conf.sch_zk_address);
    ngx_str_null(&mconf->sys_conf.sch_zk_server_id);
    ngx_str_null(&mconf->sys_conf.sch_signal_ip.key);
    ngx_str_null(&mconf->sys_conf.sch_signal_ip.value);
    ngx_str_null(&mconf->sys_conf.sch_service_ip.key);
    ngx_str_null(&mconf->sys_conf.sch_service_ip.value);
    mconf->sys_conf.sch_disk_vpath = ngx_array_create(cf->pool, TRANS_VPATH_KV_MAX,
                                                sizeof(ngx_keyval_t));
    if (mconf->sys_conf.sch_disk_vpath == NULL) {
        return NULL;
    }
    mconf->sch_zk_handle               = NULL;
    mconf->sys_conf.str_zk_address     = NULL;
    mconf->sys_conf.str_zk_trans_path  = NULL;
    mconf->sys_conf.str_zk_access_path = NULL;
    mconf->sys_conf.str_zk_stream_path = NULL;
    mconf->sys_conf.sch_zk_update      = NGX_CONF_UNSET;

    mconf->log                         = NULL;
    mconf->pool                        = NULL;
    g_VideoSysConf                     = &mconf->sys_conf;

    return mconf;
}

static char*
ngx_media_system_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_media_system_handler;

    return NGX_CONF_OK;
}
static char*
ngx_media_system_signal_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                    *value;
    ngx_uint_t                    n;
    ngx_media_system_main_conf_t *sysconf   = NULL;
    sysconf = (ngx_media_system_main_conf_t *)ngx_http_conf_get_module_main_conf(cf, ngx_media_system_module);
    if(NULL == sysconf) {
        return NGX_CONF_ERROR;
    }
    value = cf->args->elts;
    n     = cf->args->nelts;

    if((2 != n)&&(4 !=n)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "system service address conf count:[%uD] error.",n);
        return NGX_CONF_ERROR;
    }
    sysconf->sys_conf.sch_signal_ip.key = value[1];
    if(2 == n) {
        return NGX_CONF_OK;
    }

    if (ngx_strncmp(value[2].data, (u_char *) "net", 3) != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "system service address conf count:[%uD] is not net configure.",n);
        return NGX_CONF_ERROR;
    }
    sysconf->sys_conf.sch_signal_ip.value= value[3];
    return NGX_CONF_OK;
}
static char*
ngx_media_system_service_address(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                    *value;
    ngx_uint_t                    n;
    ngx_media_system_main_conf_t *sysconf   = NULL;
    sysconf = (ngx_media_system_main_conf_t *)ngx_http_conf_get_module_main_conf(cf, ngx_media_system_module);
    if(NULL == sysconf) {
        return NGX_CONF_ERROR;
    }
    value = cf->args->elts;
    n     = cf->args->nelts;

    if((2 != n)&&(4 !=n)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "system service address conf count:[%uD] error.",n);
        return NGX_CONF_ERROR;
    }
    sysconf->sys_conf.sch_service_ip.key = value[1];
    if(2 == n) {
        return NGX_CONF_OK;
    }

    if (ngx_strncmp(value[2].data, (u_char *) "net", 3) != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "system service address conf count:[%uD] is not net configure.",n);
        return NGX_CONF_ERROR;
    }
    sysconf->sys_conf.sch_service_ip.value= value[3];
    return NGX_CONF_OK;
}
static ngx_int_t
ngx_media_system_init_process(ngx_cycle_t *cycle)
{
    ngx_media_system_main_conf_t *mainconf   = NULL;
    ngx_uint_t                      i          = 0;
    ngx_keyval_t                   *kv_diskInfo=NULL;
    u_char                          cbuf[TRANS_STRING_MAX_LEN];
    u_char                         *last       = NULL;

    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);
    /* start system stat */
    if(NGX_OK != ngx_media_sys_stat_init(cycle)) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: init the system stat fail.");
        return NGX_ERROR;
    }


    mainconf = (ngx_media_system_main_conf_t *)ngx_http_cycle_get_module_main_conf(cycle, ngx_media_system_module);
    if(NULL == mainconf) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_system_module: Fail to main configuration");
        ngx_log_stderr(0, "ngx_media_system_module: Fail to main configuration");
        return NGX_ERROR;
    }

    mainconf->log   = cycle->log;
    mainconf->pool  = cycle->pool;

    /* add the system stat info */
    /* add the stat network card info to stat */
    if(0 < mainconf->sys_conf.sch_signal_ip.key.len) {
        last = ngx_cpymem(cbuf,mainconf->sys_conf.sch_signal_ip.key.data,mainconf->sys_conf.sch_signal_ip.key.len);
        *last = '\0';
        if(NGX_OK != ngx_media_sys_stat_add_networdcard(cbuf)){
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: add the signal network card:[%s] fail.",cbuf);
            return NGX_OK;
        }
    }
    if(0 < mainconf->sys_conf.sch_service_ip.key.len) {
        last = ngx_cpymem(cbuf,mainconf->sys_conf.sch_service_ip.key.data,mainconf->sys_conf.sch_service_ip.key.len);
        *last = '\0';
        if(NGX_OK != ngx_media_sys_stat_add_networdcard(cbuf)){
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: add the service network card:[%s] fail.",cbuf);
            return NGX_OK;
        }
    }


    /* add the disk infto to stat */
    if(mainconf->sys_conf.sch_disk_vpath) {
        kv_diskInfo = mainconf->sys_conf.sch_disk_vpath->elts;
        for(i = 0;i < mainconf->sys_conf.sch_disk_vpath->nelts;i++) {
            last = ngx_cpymem(cbuf,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            if(NGX_OK != ngx_media_sys_stat_add_disk(cbuf)){
                ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: add the network card:[%s] fail.",cbuf);
                return NGX_OK;
            }
        }
    }

    /* execs zookeeper are always started by the first worker */
    if (ngx_process_slot) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "ngx_media_system_module,the process:[%d] is not 0,no need start zookeepr.",ngx_process_slot);
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "ngx_media_system_module,the process:[%d] is 0,so start zookeepr.",ngx_process_slot);

    if( (NULL == mainconf->sys_conf.sch_zk_server_id.data) || (0 == mainconf->sys_conf.sch_zk_server_id.len)) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: NO server ID address,so no need register");
        return NGX_OK;
    }

    if( (NULL == mainconf->sys_conf.sch_signal_ip.key.data) || (0 == mainconf->sys_conf.sch_signal_ip.key.len)) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: NO control IP address,so no need register");
        return NGX_OK;
    }

    if(0 >= mainconf->sys_conf.sch_zk_address.len) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: NO zookeeper address,so no need register");
        return NGX_OK;
    }

    /* init the zookeepr */
    if(NULL == mainconf->sys_conf.str_zk_address) {
        mainconf->sys_conf.str_zk_address = ngx_pcalloc(cycle->pool,
                                              mainconf->sys_conf.sch_zk_address.len+1);
    }

    if(NULL == mainconf->sys_conf.str_zk_address) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "ngx_media_system_module: create the zk info fail,so no need register");
        return NGX_OK;
    }

    last = ngx_cpymem((u_char *)mainconf->sys_conf.str_zk_address,
                 mainconf->sys_conf.sch_zk_address.data,
                 mainconf->sys_conf.sch_zk_address.len);
    *last = '\0';

    if(NGX_ALLMEDIA_TYPE_TRANSCODE == (mainconf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE)) {
        if(NULL == mainconf->sys_conf.str_zk_trans_path) {
            mainconf->sys_conf.str_zk_trans_path = ngx_pcalloc(cycle->pool,
                                                  mainconf->sys_conf.sch_zk_server_id.len
                                                  + ngx_strlen(NGX_HTTP_SCH_ZK_TRANSCODE)+ 2);
        }
        last = ngx_cpymem((u_char *)mainconf->sys_conf.str_zk_trans_path,
                 NGX_HTTP_SCH_ZK_TRANSCODE,ngx_strlen(NGX_HTTP_SCH_ZK_TRANSCODE));
        *last = '/';
        last++;
        last = ngx_cpymem(last,mainconf->sys_conf.sch_zk_server_id.data,
                                mainconf->sys_conf.sch_zk_server_id.len);
        *last = '\0';
    }

    if(NGX_ALLMEDIA_TYPE_ACCESS == (mainconf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS)) {
        if(NULL == mainconf->sys_conf.str_zk_access_path) {
            mainconf->sys_conf.str_zk_access_path = ngx_pcalloc(cycle->pool,
                                                  mainconf->sys_conf.sch_zk_server_id.len
                                                  + ngx_strlen(NGX_HTTP_SCH_ZK_ACCESS)+ 2);
        }
        last = ngx_cpymem((u_char *)mainconf->sys_conf.str_zk_access_path,
                 NGX_HTTP_SCH_ZK_ACCESS,ngx_strlen(NGX_HTTP_SCH_ZK_ACCESS));
        *last = '/';
        last++;
        last = ngx_cpymem(last,mainconf->sys_conf.sch_zk_server_id.data,
                               mainconf->sys_conf.sch_zk_server_id.len);
        *last = '\0';
    }

    if(NGX_ALLMEDIA_TYPE_STREAM == (mainconf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM)) {
        if(NULL == mainconf->sys_conf.str_zk_stream_path) {
            mainconf->sys_conf.str_zk_stream_path = ngx_pcalloc(cycle->pool,
                                                  mainconf->sys_conf.sch_zk_server_id.len
                                                  + ngx_strlen(NGX_HTTP_SCH_ZK_STREAM)+ 2);
        }
        last = ngx_cpymem((u_char *)mainconf->sys_conf.str_zk_stream_path,
                 NGX_HTTP_SCH_ZK_STREAM,ngx_strlen(NGX_HTTP_SCH_ZK_STREAM));
        *last = '/';
        last++;
        last = ngx_cpymem(last,mainconf->sys_conf.sch_zk_server_id.data,
                               mainconf->sys_conf.sch_zk_server_id.len);
        *last = '\0';
    }


#if (NGX_DEBUG)
    zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);
#else
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
#endif
    mainconf->sch_zk_handle
        = zookeeper_init(mainconf->sys_conf.str_zk_address, ngx_media_system_zk_watcher, 10000, 0, mainconf, 0);
    if(NULL == mainconf->sch_zk_handle) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Fail to init zookeeper instance");
        ngx_log_stderr(0, "Fail to init zookeeper instance");
        return NGX_ERROR;
    }

    ngx_media_system_zk_init_timer(mainconf);
    return NGX_OK;
}

static void
ngx_media_system_exit_process(ngx_cycle_t *cycle)
{
    ngx_media_sys_stat_release(cycle);
    return ;
}

static void
ngx_media_system_zk_root_exsit_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    int nRet =  ZOK;
    if(rc == ZOK) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"check the zookeeper allmedia path success,so check the child path.");
        // check transcode
        if(NGX_ALLMEDIA_TYPE_TRANSCODE == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE)) {
            nRet = zoo_aexists(conf->sch_zk_handle,NGX_HTTP_SCH_ZK_TRANSCODE,0,
                                            ngx_media_system_zk_transcode_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create transcode path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
        //check access
        if(NGX_ALLMEDIA_TYPE_ACCESS == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS)) {
            nRet = zoo_aexists(conf->sch_zk_handle,NGX_HTTP_SCH_ZK_ACCESS,0,ngx_media_system_zk_access_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create access path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
        //check stream
        if(NGX_ALLMEDIA_TYPE_STREAM == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM)) {
            nRet = zoo_aexists(conf->sch_zk_handle,NGX_HTTP_SCH_ZK_STREAM,0,ngx_media_system_zk_stream_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create stream path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else {
        ngx_log_error(NGX_LOG_WARN, conf->log, 0,"the zookeeper root path is not exsti,so create it,error info:%s",zerror(rc));
        zoo_acreate(conf->sch_zk_handle, NGX_HTTP_SCH_ZK_ROOT, NULL, -1,
                        &ZOO_OPEN_ACL_UNSAFE, 0,
                        ngx_media_system_zk_root_create_completion_t, conf);

    }
}

static void
ngx_media_system_zk_root_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_media_system_main_conf_t* conf
              = (ngx_media_system_main_conf_t *)data;
    int nRet =  ZOK;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_root_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper node:%s",value);
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"create the zookeeper allmedia path success,so check the child path.");
        // check transcode
        if(NGX_ALLMEDIA_TYPE_TRANSCODE == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE)) {
            nRet = zoo_aexists(conf->sch_zk_handle,NGX_HTTP_SCH_ZK_TRANSCODE,0,
                                            ngx_media_system_zk_transcode_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create transcode path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
        //check access
        if(NGX_ALLMEDIA_TYPE_ACCESS == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS)) {
            nRet = zoo_aexists(conf->sch_zk_handle,NGX_HTTP_SCH_ZK_ACCESS,0,ngx_media_system_zk_access_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create access path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
        //check stream
        if(NGX_ALLMEDIA_TYPE_STREAM == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM)) {
            nRet = zoo_aexists(conf->sch_zk_handle,NGX_HTTP_SCH_ZK_STREAM,0,ngx_media_system_zk_stream_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create stream path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else{
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create zookeeper root node");
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
    }
}
static void
ngx_media_system_zk_transcode_exsit_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    int nRet =  ZOK;
    if(rc == ZOK) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"check the zookeeper transcode path success,so create the child path.");
        // create transcode node
        if((NGX_ALLMEDIA_TYPE_TRANSCODE == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE))
            &&(NULL != conf->sys_conf.str_zk_trans_path)){
            nRet = zoo_acreate(conf->sch_zk_handle, conf->sys_conf.str_zk_trans_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_media_system_zk_transcode_node_create_completion_t, conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create transcode node path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else {
        if(NGX_ALLMEDIA_TYPE_TRANSCODE == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE)) {
            ngx_log_error(NGX_LOG_WARN, conf->log, 0,"the zookeeper transcode path is not exsti,so create it,error info:%s",zerror(rc));
            zoo_acreate(conf->sch_zk_handle, NGX_HTTP_SCH_ZK_TRANSCODE, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, 0,
                            ngx_media_system_zk_transcode_create_completion_t, conf);
        }

    }
}
static void
ngx_media_system_zk_transcode_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_media_system_main_conf_t* conf
              = (ngx_media_system_main_conf_t *)data;
    int nRet =  ZOK;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_transcode_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper node:%s",value);
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"create the zookeeper transcode path success,so create the child path.");
        // check transcode
        if((NGX_ALLMEDIA_TYPE_TRANSCODE == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE))
            &&(NULL != conf->sys_conf.str_zk_trans_path)){
            nRet = zoo_acreate(conf->sch_zk_handle, conf->sys_conf.str_zk_trans_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_media_system_zk_transcode_node_create_completion_t, conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create transcode node path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create transcode root node");
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;

    }
}
static void
ngx_media_system_zk_transcode_node_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_media_system_main_conf_t* conf
              = (ngx_media_system_main_conf_t *)data;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_transcode_node_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper node:%s",value);
        /* try update the service stat info */
        ngx_media_system_zk_invoke_stat(conf,NGX_ALLMEDIA_TYPE_TRANSCODE);
    }
    else {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create zookeeper node");
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
    }

}

static void
ngx_media_system_zk_transcode_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_transcode_get_completion_t:ret:[%d].",rc);

    if(rc == ZOK)
    {
       if((NGX_ALLMEDIA_TYPE_TRANSCODE == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE))
            &&(NULL != conf->sys_conf.str_zk_trans_path)){
           ngx_media_system_zk_sync_system_info(conf,value,value_len,conf->sys_conf.str_zk_trans_path);
       }

    }
    else {
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "zookeeper error info:%s", zerror(rc));
    }
}


static void
ngx_media_system_zk_access_exsit_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    int nRet =  ZOK;
    if(rc == ZOK) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"check the zookeeper access path success,so create the child path.");
        // create transcode node
        if((NGX_ALLMEDIA_TYPE_ACCESS == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS))
            &&(NULL != conf->sys_conf.str_zk_access_path)){
            nRet = zoo_acreate(conf->sch_zk_handle, conf->sys_conf.str_zk_access_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_media_system_zk_access_node_create_completion_t, conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create access node path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else {
        if(NGX_ALLMEDIA_TYPE_ACCESS == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS)) {
            ngx_log_error(NGX_LOG_WARN, conf->log, 0,"the zookeeper access path is not exsti,so create it,error info:%s",zerror(rc));
            zoo_acreate(conf->sch_zk_handle, NGX_HTTP_SCH_ZK_ACCESS, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, 0,
                            ngx_media_system_zk_access_create_completion_t, conf);
        }

    }
}
static void
ngx_media_system_zk_access_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_media_system_main_conf_t* conf
              = (ngx_media_system_main_conf_t *)data;
    int nRet =  ZOK;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_access_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper node:%s",value);
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"create the zookeeper access path success,so create the child path.");
        // check transcode
        if((NGX_ALLMEDIA_TYPE_ACCESS == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS))
            &&(NULL != conf->sys_conf.str_zk_access_path)){
            nRet = zoo_acreate(conf->sch_zk_handle, conf->sys_conf.str_zk_access_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_media_system_zk_access_node_create_completion_t, conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create access node path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create access root node");
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
    }
}
static void
ngx_media_system_zk_access_node_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_media_system_main_conf_t* conf
              = (ngx_media_system_main_conf_t *)data;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_access_node_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper node:%s",value);
        /* try update the service stat info */
        ngx_media_system_zk_invoke_stat(conf,NGX_ALLMEDIA_TYPE_ACCESS);

    }
    else {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create zookeeper access node");
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
    }
}
static void
ngx_media_system_zk_access_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_access_get_completion_t:ret:[%d].",rc);

    if(rc == ZOK)
    {
       if((NGX_ALLMEDIA_TYPE_ACCESS == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS))
            &&(NULL != conf->sys_conf.str_zk_access_path)){
           ngx_media_system_zk_sync_system_info(conf,value,value_len,conf->sys_conf.str_zk_access_path);
       }

    }
    else {
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "zookeeper error info:%s", zerror(rc));
    }
}


static void
ngx_media_system_zk_stream_exsit_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    int nRet =  ZOK;
    if(rc == ZOK) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"check the zookeeper stream path success,so create the child path.");
        // create transcode node
        if((NGX_ALLMEDIA_TYPE_STREAM == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM))
            &&(NULL != conf->sys_conf.str_zk_stream_path)){
            nRet = zoo_acreate(conf->sch_zk_handle, conf->sys_conf.str_zk_stream_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_media_system_zk_stream_node_create_completion_t, conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create stream node path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else {
        if(NGX_ALLMEDIA_TYPE_STREAM == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM)) {
            ngx_log_error(NGX_LOG_WARN, conf->log, 0,"the zookeeper stream path is not exsti,so create it,error info:%s",zerror(rc));
            zoo_acreate(conf->sch_zk_handle, NGX_HTTP_SCH_ZK_STREAM, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, 0,
                            ngx_media_system_zk_stream_create_completion_t, conf);
        }

    }
}
static void
ngx_media_system_zk_stream_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_media_system_main_conf_t* conf
              = (ngx_media_system_main_conf_t *)data;
    int nRet =  ZOK;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_stream_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper node:%s",value);
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"create the zookeeper stream path success,so create the child path.");
        // check transcode
        if((NGX_ALLMEDIA_TYPE_STREAM == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM))
            &&(NULL != conf->sys_conf.str_zk_stream_path)){
            nRet = zoo_acreate(conf->sch_zk_handle, conf->sys_conf.str_zk_stream_path, NULL, -1,
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,
                            ngx_media_system_zk_stream_node_create_completion_t, conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create stream node path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }
        }
    }
    else {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create stream root node");
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
    }
}


static void
ngx_media_system_zk_stream_node_create_completion_t(int rc, const char *value, const void *data)
{
    ngx_media_system_main_conf_t* conf
              = (ngx_media_system_main_conf_t *)data;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_stream_node_create_completion_t: ret:[%d]!",rc);

    if (ZOK == rc || ZNODEEXISTS == rc) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0, "create zookeeper node:%s",value);
        /* try update the service stat info */
        ngx_media_system_zk_invoke_stat(conf,NGX_ALLMEDIA_TYPE_STREAM);
    }
    else {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to create zookeeper access node");
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
    }
}

static void
ngx_media_system_zk_stream_get_completion_t(int rc, const char *value, int value_len, const struct Stat *stat, const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_stream_get_completion_t:ret:[%d].",rc);

    if(rc == ZOK)
    {
       if((NGX_ALLMEDIA_TYPE_STREAM == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM))
            &&(NULL != conf->sys_conf.str_zk_stream_path)){
           ngx_media_system_zk_sync_system_info(conf,value,value_len,conf->sys_conf.str_zk_stream_path);
       }

    }
    else {
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "zookeeper error info:%s", zerror(rc));
    }
}


static void
ngx_media_system_zk_watcher(zhandle_t *zh, int type,int state, const char *path,void *watcherCtx)
{
    ngx_media_system_main_conf_t* conf
                = (ngx_media_system_main_conf_t*)watcherCtx;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_watcher: stat:[%d]!",state);

    if(type == ZOO_SESSION_EVENT) {
        if(state == ZOO_CONNECTED_STATE) {
            //check allmedia root path
            int nRet = zoo_aexists(conf->sch_zk_handle,NGX_HTTP_SCH_ZK_ROOT,0,ngx_media_system_zk_root_exsit_completion_t,conf);
            if(ZOK != nRet) {
                ngx_log_error(NGX_LOG_WARN, conf->log, 0,"create root path fail,so disconnect and try again later.");
                zookeeper_close(conf->sch_zk_handle);
                conf->sch_zk_handle = NULL;
            }

        }
        else if( state == ZOO_AUTH_FAILED_STATE) {
            zookeeper_close(zh);
            conf->sch_zk_handle = NULL;
            ngx_log_error(NGX_LOG_ERR, conf->log, 0, "zookeeper Authentication failure");
        }
        else if( state == ZOO_EXPIRED_SESSION_STATE) {
            zookeeper_close(zh);
            conf->sch_zk_handle
                    = zookeeper_init(conf->sys_conf.str_zk_address,
                                     ngx_media_system_zk_watcher,
                                     10000, 0, conf, 0);
            if (NULL == conf->sch_zk_handle) {
                ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to init zookeeper instance");
            }
        }
    }
}

static void
ngx_media_system_zk_init_timer(ngx_media_system_main_conf_t* conf)
{
    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_module:start zookeeper register timer.");
    ngx_memzero(&conf->sch_zk_timer, sizeof(ngx_event_t));
    conf->sch_zk_timer.handler = ngx_media_system_zk_check_timeout;
    conf->sch_zk_timer.log     = conf->log;
    conf->sch_zk_timer.data    = conf;

    ngx_add_timer(&conf->sch_zk_timer, conf->sys_conf.sch_zk_update);
}

static void
ngx_media_system_zk_check_timeout(ngx_event_t *ev)
{
    ngx_media_system_main_conf_t* conf
            = (ngx_media_system_main_conf_t*)ev->data;
    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_module:update zookeeper register info.");

    if(NULL == conf->sch_zk_handle) {
        conf->sch_zk_handle
                    = zookeeper_init(conf->sys_conf.str_zk_address,
                                     ngx_media_system_zk_watcher,
                                     10000, 0, conf, 0);
        if (NULL == conf->sch_zk_handle) {
            ngx_log_error(NGX_LOG_ERR, conf->log, 0, "Fail to init zookeeper instance");
        }
    }
    else {
        ngx_media_system_zk_invoke_stat(conf,NGX_ALLMEDIA_TYPE_ALL);
    }
    ngx_add_timer(&conf->sch_zk_timer, conf->sys_conf.sch_zk_update);
}
static void
ngx_media_system_zk_sync_system_info(ngx_media_system_main_conf_t* conf, const char *value, int value_len,char* zk_path)
{
    ngx_uint_t i              = 0;
    ngx_uint_t ullCpuPer      = 0;
    ngx_uint_t ullMemtotal    = 0;
    ngx_uint_t ullMemused     = 0;
    ngx_uint_t ulTotalSize    = 0;
    ngx_uint_t ulUsedRecvSize = 0;
    ngx_uint_t ulUsedSendSize = 0;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    u_char    *last           = NULL;
    ngx_keyval_t  *kv_diskInfo=NULL;

    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_sync_system_info,begin");

    cJSON *root = cJSON_Parse(value);
    if(root == NULL)
    {
        root = cJSON_CreateObject();
    }

    cJSON *taskObj,*taskCount,*taskMax;
    cJSON *RtmpObj,*RtmpCount,*RtmpMax;
    cJSON *HlsObj,*HlsCount,*HlsMax;
    cJSON *RtspObj,*RtspCount,*RtspMax;
    cJSON *cpuObj, *memObj,*memTotalObj,*memUsedObj,*memUnitObj;
    cJSON *signalipObj,*serviceipObj,*ipObj,*ipUnitObj;
    cJSON *totalSize,*recvSize,*sendSize;
    cJSON *diskList,*diskObj;
    cJSON *vpath,*path,*diskSize,*usedSize,*diskUnitObj;


    ngx_uint_t task_count = ngx_media_task_get_cur_count();
    ngx_uint_t task_max   = ngx_media_license_task_count();

    ngx_uint_t rtmp_count = ngx_media_license_rtmp_current();
    ngx_uint_t rtmp_max   = ngx_media_license_rtmp_channle();

    ngx_uint_t hls_count  = ngx_media_license_hls_current();
    ngx_uint_t hls_max    = ngx_media_license_hls_channle();

    ngx_uint_t rtsp_count = ngx_media_license_rtsp_current();
    ngx_uint_t rtsp_max   = ngx_media_license_rtsp_channle();

    /* 1.get the trans task ,rtmp,hls,rtsp info:total,capacity */
    /* task */
    taskObj = cJSON_GetObjectItem(root,"task");
    if(taskObj == NULL) {
        cJSON_AddItemToObject(root,"task",taskObj = cJSON_CreateObject());
    }
    taskCount = cJSON_GetObjectItem(taskObj,"taskcount");
    if(taskCount == NULL) {
        cJSON_AddItemToObject(taskObj,"taskcount",taskCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(taskCount, task_count);
    taskMax = cJSON_GetObjectItem(taskObj,"taskmax");
    if(taskMax == NULL) {
        cJSON_AddItemToObject(taskObj,"taskmax",taskMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(taskMax, task_max);
    /* rtmp */
    RtmpObj = cJSON_GetObjectItem(root,"rtmp");
    if(RtmpObj == NULL) {
        cJSON_AddItemToObject(root,"rtmp",RtmpObj = cJSON_CreateObject());
    }
    RtmpCount = cJSON_GetObjectItem(RtmpObj,"rtmpcount");
    if(RtmpCount == NULL) {
        cJSON_AddItemToObject(RtmpObj,"rtmpcount",RtmpCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtmpCount, rtmp_count);
    RtmpMax = cJSON_GetObjectItem(RtmpObj,"rtmpmax");
    if(RtmpMax == NULL) {
        cJSON_AddItemToObject(RtmpObj,"rtmpmax",RtmpMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtmpMax, rtmp_max);
    /* hls */
    HlsObj = cJSON_GetObjectItem(root,"hls");
    if(HlsObj == NULL) {
        cJSON_AddItemToObject(root,"hls",HlsObj = cJSON_CreateObject());
    }
    HlsCount = cJSON_GetObjectItem(HlsObj,"hlscount");
    if(HlsCount == NULL) {
        cJSON_AddItemToObject(HlsObj,"hlscount",HlsCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(HlsCount, hls_count);
    HlsMax = cJSON_GetObjectItem(HlsObj,"hlsmax");
    if(HlsMax == NULL) {
        cJSON_AddItemToObject(HlsObj,"hlsmax",HlsMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(HlsMax, hls_max);
    /* rtsp */
    RtspObj = cJSON_GetObjectItem(root,"rtsp");
    if(RtspObj == NULL) {
        cJSON_AddItemToObject(root,"rtsp",RtspObj = cJSON_CreateObject());
    }
    RtspCount = cJSON_GetObjectItem(RtspObj,"rtspcount");
    if(RtspCount == NULL) {
        cJSON_AddItemToObject(RtspObj,"rtspcount",RtspCount = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtspCount, rtsp_count);
    RtspMax = cJSON_GetObjectItem(RtspObj,"rtspmax");
    if(RtspMax == NULL) {
        cJSON_AddItemToObject(RtspObj,"rtspmax",RtspMax = cJSON_CreateNumber(0));
    }
    cJSON_SetNumberValue(RtspMax, rtsp_max);
    /* 2.get the system info:cpu,memory,disk,network */
    /*cpu,memory*/
    ngx_media_sys_stat_get_cpuinfo(&ullCpuPer);
    ngx_media_sys_stat_get_memoryinfo(&ullMemtotal,&ullMemused);

    cpuObj = cJSON_GetObjectItem(root,"cpu");

    if(cpuObj == NULL)
        cJSON_AddItemToObject(root,"cpu",cpuObj = cJSON_CreateNumber(0));
    cJSON_SetNumberValue(cpuObj, ullCpuPer);

    memObj = cJSON_GetObjectItem(root,"mem");
    if(memObj == NULL)
        cJSON_AddItemToObject(root,"mem",memObj = cJSON_CreateObject());

    memUnitObj = cJSON_GetObjectItem(memObj,"uint");
    if(memUnitObj == NULL)
        cJSON_AddItemToObject(memObj,"uint",memUnitObj = cJSON_CreateString("MB"));

    memTotalObj = cJSON_GetObjectItem(memObj,"total");
    if(memTotalObj == NULL)
        cJSON_AddItemToObject(memObj,"total",memTotalObj = cJSON_CreateNumber(0));
    cJSON_SetNumberValue(memTotalObj, ullMemtotal);

    memUsedObj = cJSON_GetObjectItem(memObj,"used");
    if(memUsedObj == NULL)
        cJSON_AddItemToObject(memObj,"used",memUsedObj = cJSON_CreateNumber(0));
    cJSON_SetNumberValue(memUsedObj, ullMemused);

    /*signal ip network*/
    if(0 < conf->sys_conf.sch_signal_ip.key.len){
        signalipObj = cJSON_GetObjectItem(root,"signalip");
        last = ngx_cpymem(cbuf,conf->sys_conf.sch_signal_ip.key.data,conf->sys_conf.sch_signal_ip.key.len);
        *last = '\0';
        if(signalipObj == NULL){
            cJSON_AddItemToObject(root,"signalip",signalipObj = cJSON_CreateObject());
        }
        ngx_media_sys_stat_get_networkcardinfo(cbuf,&ulTotalSize,&ulUsedRecvSize,&ulUsedSendSize);

        if((NULL != conf->sys_conf.sch_signal_ip.value.data)&&(0 < conf->sys_conf.sch_signal_ip.value.len)) {
            /* set the firewall address */
            last = ngx_cpymem(cbuf,conf->sys_conf.sch_signal_ip.value.data,conf->sys_conf.sch_signal_ip.value.len);
            *last = '\0';
        }

        ipObj = cJSON_GetObjectItem(signalipObj,"ip");
        if(ipObj == NULL){
            cJSON_AddItemToObject(signalipObj,"ip",ipObj = cJSON_CreateString((char*)&cbuf[0]));
        }

        ipUnitObj = cJSON_GetObjectItem(signalipObj,"uint");
        if(ipUnitObj == NULL){
            cJSON_AddItemToObject(signalipObj,"uint",ipUnitObj = cJSON_CreateString("kbps"));
        }

        totalSize = cJSON_GetObjectItem(signalipObj,"total");
        if(totalSize == NULL){
            cJSON_AddItemToObject(signalipObj,"total",totalSize = cJSON_CreateNumber(0));
        }
        recvSize = cJSON_GetObjectItem(signalipObj,"recv");
        if(recvSize == NULL){
            cJSON_AddItemToObject(signalipObj,"recv",recvSize = cJSON_CreateNumber(0));
        }
        sendSize = cJSON_GetObjectItem(signalipObj,"send");
        if(sendSize == NULL){
            cJSON_AddItemToObject(signalipObj,"send",sendSize = cJSON_CreateNumber(0));
        }

        cJSON_SetNumberValue(totalSize, ulTotalSize);
        cJSON_SetNumberValue(recvSize, ulUsedRecvSize);
        cJSON_SetNumberValue(sendSize, ulUsedSendSize);

    }

    /*service ip network*/
    if(0 < conf->sys_conf.sch_service_ip.key.len){
        serviceipObj = cJSON_GetObjectItem(root,"serviceip");
        last = ngx_cpymem(cbuf,conf->sys_conf.sch_service_ip.key.data,conf->sys_conf.sch_service_ip.key.len);
        *last = '\0';
        if(serviceipObj == NULL){
            cJSON_AddItemToObject(root,"serviceip",serviceipObj = cJSON_CreateObject());
        }
        ngx_media_sys_stat_get_networkcardinfo(cbuf,&ulTotalSize,&ulUsedRecvSize,&ulUsedSendSize);

        if((NULL != conf->sys_conf.sch_service_ip.value.data)&&(0 < conf->sys_conf.sch_service_ip.value.len)) {
            /* set the firewall address */
            last = ngx_cpymem(cbuf,conf->sys_conf.sch_service_ip.value.data,conf->sys_conf.sch_service_ip.value.len);
            *last = '\0';
        }

        ipObj = cJSON_GetObjectItem(serviceipObj,"ip");
        if(ipObj == NULL){
            cJSON_AddItemToObject(serviceipObj,"ip",ipObj = cJSON_CreateString((char*)&cbuf[0]));
        }
        ipUnitObj = cJSON_GetObjectItem(serviceipObj,"uint");
        if(ipUnitObj == NULL){
            cJSON_AddItemToObject(serviceipObj,"uint",ipUnitObj = cJSON_CreateString("kbps"));
        }
        totalSize = cJSON_GetObjectItem(serviceipObj,"total");
        if(totalSize == NULL){
            cJSON_AddItemToObject(serviceipObj,"total",totalSize = cJSON_CreateNumber(0));
        }
        recvSize = cJSON_GetObjectItem(serviceipObj,"recv");
        if(recvSize == NULL){
            cJSON_AddItemToObject(serviceipObj,"recv",recvSize = cJSON_CreateNumber(0));
        }
        sendSize = cJSON_GetObjectItem(serviceipObj,"send");
        if(sendSize == NULL){
            cJSON_AddItemToObject(serviceipObj,"send",sendSize = cJSON_CreateNumber(0));
        }

        cJSON_SetNumberValue(totalSize, ulTotalSize);
        cJSON_SetNumberValue(recvSize, ulUsedRecvSize);
        cJSON_SetNumberValue(sendSize, ulUsedSendSize);

    }

    /*disk stat info*/
    if(0 < conf->sys_conf.sch_disk_vpath->nelts) {
        diskList = cJSON_GetObjectItem(root,"diskinfolist");
        if(diskList == NULL){
            cJSON_AddItemToObject(root,"diskinfolist",diskList = cJSON_CreateArray());
        }
        kv_diskInfo = conf->sys_conf.sch_disk_vpath->elts;
        for(i = 0;i < conf->sys_conf.sch_disk_vpath->nelts;i++) {
            last = ngx_cpymem(cbuf,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            ngx_media_sys_stat_get_diskinfo(cbuf,&ulTotalSize,&ulUsedRecvSize);
            diskObj = cJSON_GetArrayItem(diskList,i);
            if(diskObj == NULL) {
                cJSON_AddItemToObject(diskList,"diskinfo",diskObj = cJSON_CreateObject());
            }

            vpath = cJSON_GetObjectItem(diskObj,"vptah");
            last = ngx_cpymem(cbuf,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len);
            *last = '\0';
            if(vpath == NULL) {
                cJSON_AddItemToObject(diskObj,"vptah",vpath = cJSON_CreateString((char*)&cbuf[0]));
            }
            path = cJSON_GetObjectItem(diskObj,"path");
            last = ngx_cpymem(cbuf,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            if(path == NULL) {
                cJSON_AddItemToObject(diskObj,"path",vpath = cJSON_CreateString((char*)&cbuf[0]));
            }


            diskUnitObj = cJSON_GetObjectItem(diskObj,"uint");
            if(diskUnitObj == NULL)
                cJSON_AddItemToObject(diskObj,"uint",diskUnitObj = cJSON_CreateString("MB"));

            diskSize = cJSON_GetObjectItem(diskObj,"diskSize");
            if(diskSize == NULL) {
                cJSON_AddItemToObject(diskObj,"diskSize",vpath = cJSON_CreateNumber(0));
            }

            usedSize = cJSON_GetObjectItem(diskObj,"usedSize");
            if(usedSize == NULL) {
                cJSON_AddItemToObject(diskObj,"usedSize",usedSize = cJSON_CreateNumber(0));
            }

            cJSON_SetNumberValue(diskSize, ulTotalSize);
            cJSON_SetNumberValue(usedSize, ulUsedRecvSize);
        }
    }

    /* 3.build the json infomation and update the zookeeper node */

    char * json_buf = cJSON_PrintUnformatted(root);

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_update_stat:value:[%s].",json_buf);

    zoo_aset(conf->sch_zk_handle,zk_path,json_buf,ngx_strlen(json_buf),-1,ngx_media_system_zk_stat_completion_t,conf);

    free(json_buf);

    cJSON_Delete(root);
}

static void
ngx_media_system_zk_stat_completion_t(int rc, const struct Stat *stat,const void *data)
{
    ngx_media_system_main_conf_t* conf = (ngx_media_system_main_conf_t*)data;
    if(rc == ZOK) {
        ngx_log_error(NGX_LOG_INFO, conf->log, 0,"update to service data to zookeeper success.");
    }
    else {
        zookeeper_close(conf->sch_zk_handle);
        conf->sch_zk_handle = NULL;
        ngx_log_error(NGX_LOG_ERR, conf->log, 0,"update to upstream workload is failed,error info:%s",zerror(rc));
    }
}
static void
ngx_media_system_zk_invoke_stat(ngx_media_system_main_conf_t* conf,ngx_uint_t flag)
{
    int rc;

    ngx_log_error(NGX_LOG_DEBUG, conf->log, 0, "ngx_media_system_zk_invoke_stat.");

    if(NULL == conf->sch_zk_handle) {
        ngx_log_error(NGX_LOG_ERR, conf->log, 0, "invoke the stat info ,but the zk handle fail.");
        return;
    }

    if(zoo_state(conf->sch_zk_handle) != ZOO_CONNECTED_STATE) {
        return;
    }
    if((NGX_ALLMEDIA_TYPE_TRANSCODE == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_TRANSCODE))
       &&(NULL != conf->sys_conf.str_zk_trans_path)){
        rc = zoo_aget(conf->sch_zk_handle,conf->sys_conf.str_zk_trans_path, 0,
                        ngx_media_system_zk_transcode_get_completion_t, conf);
        if(rc != ZOK) {
            zookeeper_close(conf->sch_zk_handle);
            conf->sch_zk_handle = NULL;
            ngx_log_error(NGX_LOG_ERR, conf->log, 0, "get transcode path is failed, %s,error info:%s",conf->sys_conf.str_zk_trans_path,zerror(rc));
            return;
        }
    }
    if((NGX_ALLMEDIA_TYPE_ACCESS == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_ACCESS))
       &&(NULL != conf->sys_conf.str_zk_access_path)){
        rc = zoo_aget(conf->sch_zk_handle,conf->sys_conf.str_zk_access_path, 0,
                        ngx_media_system_zk_access_get_completion_t, conf);
        if(rc != ZOK) {
            zookeeper_close(conf->sch_zk_handle);
            conf->sch_zk_handle = NULL;
            ngx_log_error(NGX_LOG_ERR, conf->log, 0, "get access path is failed, %s,error info:%s",conf->sys_conf.str_zk_access_path,zerror(rc));
            return;
        }
    }
    if((NGX_ALLMEDIA_TYPE_STREAM == (conf->sys_conf.sch_server_flags&NGX_ALLMEDIA_TYPE_STREAM))
       &&(NULL != conf->sys_conf.str_zk_stream_path)){
        rc = zoo_aget(conf->sch_zk_handle,conf->sys_conf.str_zk_stream_path, 0,
                        ngx_media_system_zk_stream_get_completion_t, conf);
        if(rc != ZOK) {
            zookeeper_close(conf->sch_zk_handle);
            conf->sch_zk_handle = NULL;
            ngx_log_error(NGX_LOG_ERR, conf->log, 0, "get stream path is failed, %s,error info:%s",conf->sys_conf.str_zk_stream_path,zerror(rc));
            return;
        }
    }
}


static ngx_int_t
ngx_media_system_disk_stat(ngx_http_request_t *r,ngx_chain_t* out)
{
    xmlDocPtr resp_doc    = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, disk_node = NULL,vpath_node = NULL;/* node pointers */
    u_char*  pbuff        = NULL;
    u_char* last          = NULL;
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    ngx_media_system_main_conf_t* conf = NULL;
    ngx_uint_t i              = 0;
    ngx_uint_t ulTotalSize    = 0;
    ngx_uint_t ulUsedSize     = 0;
    ngx_uint_t ulFreeSize     = 0;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    ngx_keyval_t  *kv_diskInfo=NULL;

    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);

    conf = ngx_http_get_module_main_conf(r, ngx_media_system_module);
    if(NULL == conf)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx_media_system_disk_stat,get main conf fail.");
        return NGX_ERROR;
    }


    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_media_system_disk_stat");

    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(resp_doc, root_node);

    /*disk stat info*/
    if(0 < conf->sys_conf.sch_disk_vpath->nelts) {
        disk_node = xmlNewNode(NULL, BAD_CAST SYSTEM_XML_DISK);
        xmlNewProp(disk_node, BAD_CAST COMMON_XML_COMMAND, BAD_CAST SYSTEM_COMMAND_DISK_STAT);
        xmlAddChild(root_node, disk_node);

        kv_diskInfo = conf->sys_conf.sch_disk_vpath->elts;
        for(i = 0;i < conf->sys_conf.sch_disk_vpath->nelts;i++) {
            last = ngx_cpymem(cbuf,kv_diskInfo[i].value.data,kv_diskInfo[i].value.len);
            *last = '\0';
            ngx_media_sys_stat_get_diskinfo(cbuf,&ulTotalSize,&ulUsedSize);
            vpath_node = xmlNewNode(NULL, BAD_CAST SYSTEM_XML_DISK_VPATH);

            last = ngx_cpymem(cbuf,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len);
            *last = '\0';
            xmlNewProp(vpath_node, BAD_CAST COMMON_XML_NAME, BAD_CAST cbuf);

            last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%i",ulTotalSize);
            *last = '\0';
            xmlNewProp(vpath_node, BAD_CAST COMMON_XML_SIZE, BAD_CAST cbuf);

            ulFreeSize = ulTotalSize - ulUsedSize;
            last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%i",ulFreeSize);
            *last = '\0';
            xmlNewProp(vpath_node, BAD_CAST COMMON_XML_FREE, BAD_CAST cbuf);
            xmlAddChild(disk_node, vpath_node);
        }
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    return NGX_OK;
}
static ngx_int_t
ngx_media_system_disk_list_file(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    xmlNodePtr vpath_node = NULL,file_node = NULL;/* node pointers */
    vpath_node = (xmlNodePtr)ctx->data;

    u_char* begin = path->data;
    u_char* end  = path->data + path->len;
    u_char* last = NULL;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);
    ngx_str_t   file;
    ngx_str_null(&file);

    last = ngx_video_strrchr(begin,end,'/');
    if(NULL != last) {
        file.data = last + 1;
        file.len  = end - last;
    }
    else
    {
        file.data = path->data;
        file.len  = path->len;
    }

    last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%V",&file);
    *last = '\0';

    file_node = xmlNewNode(NULL, BAD_CAST SYSTEM_XML_DISK_FILE);

    xmlNewProp(file_node, BAD_CAST COMMON_XML_NAME, BAD_CAST cbuf);

    last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%i",ctx->size);
    *last = '\0';
    xmlNewProp(file_node, BAD_CAST COMMON_XML_SIZE, BAD_CAST cbuf);

    xmlAddChild(vpath_node, file_node);

    return NGX_OK;
}
static ngx_int_t
ngx_media_system_disk_list_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    xmlNodePtr vpath_node = NULL,dir_node = NULL;/* node pointers */
    vpath_node = (xmlNodePtr)ctx->data;

    u_char* begin = path->data;
    u_char* end  = path->data + path->len - 1; /* skip the last '/' */
    u_char* last = NULL;
    u_char     cbuf[TRANS_STRING_MAX_LEN];
    ngx_memzero(&cbuf,TRANS_STRING_MAX_LEN);
    ngx_str_t   dir;
    ngx_str_null(&dir);

    last = ngx_video_strrchr(begin,end,'/');
    if(NULL != last) {
        dir.data = last;
        dir.len  = end - last + 1;
    }
    else
    {
        dir.data = path->data;
        dir.len  = path->len;
    }

    last = ngx_snprintf(cbuf,TRANS_STRING_MAX_LEN,"%V",&dir);
    *last = '\0';

    dir_node = xmlNewNode(NULL, BAD_CAST SYSTEM_XML_DISK_DIR);

    xmlNewProp(dir_node, BAD_CAST COMMON_XML_NAME, BAD_CAST cbuf);

    xmlAddChild(vpath_node, dir_node);
    return NGX_OK;
}
static ngx_int_t
ngx_media_system_disk_list_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}


static ngx_int_t
ngx_media_system_disk_list(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out)
{
    xmlNodePtr curNode    = NULL;
    xmlChar*   attr_name  = NULL;
    xmlChar*   attr_dir   = NULL;
    xmlDocPtr  resp_doc    = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, disk_node = NULL,vpath_node = NULL;/* node pointers */
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    u_char*  pbuff        = NULL;
    ngx_keyval_t  *kv_diskInfo=NULL;
    ngx_uint_t i              = 0;
    ngx_flag_t find           = 0;
    u_char    *last           = NULL;
    ngx_file_info_t           fi;
    size_t     size           = 0;
    ngx_tree_ctx_t            tree;
    ngx_str_t  path;

    ngx_str_null(&path);

    ngx_media_system_main_conf_t* conf = NULL;
    conf = ngx_http_get_module_main_conf(r, ngx_media_system_module);
    if(NULL == conf)
    {
        return NGX_ERROR;
    }

    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(resp_doc, root_node);

    disk_node = xmlNewNode(NULL, BAD_CAST SYSTEM_XML_DISK);
    xmlNewProp(disk_node, BAD_CAST COMMON_XML_COMMAND, BAD_CAST SYSTEM_COMMAND_DISK_LIST);
    xmlAddChild(root_node, disk_node);

    /* operate node */
    curNode = Node->children;
    while(NULL != curNode) {

        if (xmlStrcmp(curNode->name, BAD_CAST SYSTEM_XML_DISK_VPATH)) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,the node is not vpath");
            curNode = curNode->next;
            continue;
        }

        attr_name = xmlGetProp(curNode,BAD_CAST COMMON_XML_NAME);
        if(NULL == attr_name) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath name not found.");
            curNode = curNode->next;
            continue;
        }
        attr_dir = xmlGetProp(curNode,BAD_CAST SYSTEM_XML_DISK_DIR);
        if(NULL == attr_dir) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath dir not found.");
            curNode = curNode->next;
            continue;
        }

        /* map the vpath to path of file system */
        if(0 == conf->sys_conf.sch_disk_vpath->nelts) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath is not configure.");
            curNode = curNode->next;
            continue;
        }
        find = 0;
        kv_diskInfo = conf->sys_conf.sch_disk_vpath->elts;
        for(i = 0;i < conf->sys_conf.sch_disk_vpath->nelts;i++) {
            if(0 == ngx_strncmp(attr_name,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len)) {
                find = 1;
                break;
            }
        }

        if(!find) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath is not found.");
            curNode = curNode->next;
            continue;
        }
        path.len = kv_diskInfo[i].value.len + ngx_strlen(attr_dir);
        size = path.len;
        path.data = ngx_pcalloc(r->pool,size+1);

        last = ngx_snprintf(path.data,size, "%V%s", &kv_diskInfo[i].value,attr_dir);
        *last = '\0';

        /* check all the task xml file,and start the task */
        if (ngx_link_info(path.data, &fi) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_WARN, conf->log, 0,
                              "ngx_media_system_disk_list,link the dir:[%V] fail.",&path);
            curNode = curNode->next;
            continue;
        }
        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_WARN, conf->log, 0,
                              "ngx_media_system_disk_list,the:[%V] is not a dir.",&path);
            curNode = curNode->next;
            continue;
        }

        vpath_node = xmlNewNode(NULL, BAD_CAST SYSTEM_XML_DISK_VPATH);

        xmlNewProp(vpath_node, BAD_CAST COMMON_XML_NAME, BAD_CAST attr_name);
        xmlNewProp(vpath_node, BAD_CAST SYSTEM_XML_DISK_DIR, BAD_CAST attr_dir);

        /* walk the list dir */
        tree.init_handler = NULL;
        tree.file_handler = ngx_media_system_disk_list_file;
        tree.pre_tree_handler = ngx_media_system_disk_list_dir;
        tree.post_tree_handler = ngx_media_system_disk_list_noop;
        tree.spec_handler = ngx_media_system_disk_list_noop;
        tree.data = vpath_node;
        tree.alloc = 0;
        tree.log  = r->connection->log;
        if (ngx_walk_tree(&tree, &path) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, conf->log, 0,
                              "ngx_media_system_disk_list,walk tree:[%V] fail.",&path);
            xmlFreeNode(vpath_node);
            curNode = curNode->next;
            continue;
        }
        xmlAddChild(disk_node, vpath_node);
        curNode = curNode->next;
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_media_system_disk_delete_file(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_system_disk_delete_file,delete file:[%V].",path);

    if (ngx_delete_file(path->data) == NGX_FILE_ERROR) {

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_system_disk_delete_file,delete file:[%V] fail.",path);
    }

    return NGX_OK;
}

static ngx_int_t
ngx_media_system_disk_delete_dir(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    ngx_log_error(NGX_LOG_DEBUG, ctx->log, 0,
                          "ngx_media_system_disk_delete_dir,delete dir:[%V].",path);

    if (ngx_delete_dir(path->data) == NGX_FILE_ERROR) {

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "ngx_media_system_disk_delete_dir,delete dir:[%V] fail.",path);
    }

    return NGX_OK;

}

static ngx_int_t
ngx_media_system_disk_delete_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}

static ngx_int_t
ngx_media_system_disk_delete(ngx_http_request_t *r,xmlNodePtr Node,ngx_chain_t* out)
{
    xmlNodePtr curNode    = NULL;
    xmlNodePtr childNode  = NULL;
    xmlChar*   attr_name  = NULL;
    xmlChar*   attr_dir   = NULL;
    xmlDocPtr  resp_doc    = NULL;       /* document pointer */
    xmlNodePtr root_node  = NULL, disk_node = NULL;/* node pointers */
    xmlChar *xmlbuff;
    int buffersize;
    ngx_buf_t *b;
    u_char*  pbuff        = NULL;
    ngx_keyval_t  *kv_diskInfo=NULL;
    ngx_uint_t i              = 0;
    ngx_flag_t find           = 0;
    u_char    *last           = NULL;
    ngx_file_info_t           fi;
    ngx_tree_ctx_t            tree;
    ngx_str_t  path;
    ngx_str_t  sub_path;
    size_t     size           = 0;

    ngx_str_null(&path);

    ngx_media_system_main_conf_t* conf = NULL;
    conf = ngx_http_get_module_main_conf(r, ngx_media_system_module);
    if(NULL == conf)
    {
        return NGX_ERROR;
    }

    tree.init_handler = NULL;
    tree.file_handler = ngx_media_system_disk_delete_file;
    tree.pre_tree_handler = ngx_media_system_disk_delete_noop;
    tree.post_tree_handler = ngx_media_system_disk_delete_dir;
    tree.spec_handler = ngx_media_system_disk_delete_file;
    tree.data = NULL;
    tree.alloc = 0;
    tree.log = r->connection->log;

    /* Creates a new document, a node and set it as a root node*/
    resp_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST COMMON_XML_RESP);
    xmlNewProp(root_node, BAD_CAST "version", BAD_CAST "1.0");
    xmlNewProp(root_node, BAD_CAST "err_code", BAD_CAST "0");
    xmlNewProp(root_node, BAD_CAST "err_msg", BAD_CAST "success");
    xmlDocSetRootElement(resp_doc, root_node);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "ngx_media_system_disk_delete,delete file or dir begin.");
    /* operate node */
    curNode = Node->children;
    while(NULL != curNode) {

        if (xmlStrcmp(curNode->name, BAD_CAST SYSTEM_XML_DISK_VPATH)) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,the node is not vpath");
            curNode = curNode->next;
            continue;
        }

        attr_name = xmlGetProp(curNode,BAD_CAST COMMON_XML_NAME);
        if(NULL == attr_name) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath name not found.");
            curNode = curNode->next;
            continue;
        }
        attr_dir = xmlGetProp(curNode,BAD_CAST SYSTEM_XML_DISK_DIR);
        if(NULL == attr_dir) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath dir not found.");
            curNode = curNode->next;
            continue;
        }

        /* map the vpath to path of file system */
        if(0 == conf->sys_conf.sch_disk_vpath->nelts) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath is not configure.");
            curNode = curNode->next;
            continue;
        }
        find = 0;
        kv_diskInfo = conf->sys_conf.sch_disk_vpath->elts;
        for(i = 0;i < conf->sys_conf.sch_disk_vpath->nelts;i++) {
            if(0 == ngx_strncmp(attr_name,kv_diskInfo[i].key.data,kv_diskInfo[i].key.len)) {
                find = 1;
                break;
            }
        }

        if(!find) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "ngx_media_system_disk_list,vpath is not found.");
            curNode = curNode->next;
            continue;
        }
        path.len = kv_diskInfo[i].value.len + ngx_strlen(attr_dir);
        size = path.len;
        path.data = ngx_pcalloc(r->pool, size + 1);

        last = ngx_snprintf(path.data,size, "%V%s", &kv_diskInfo[i].value,attr_dir);
        *last = '\0';

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP,  r->connection->log, 0,
                              "ngx_media_system_disk_delete,dir:[%V].",&path);

        /* check all the task xml file,and start the task */
        if (ngx_link_info(path.data, &fi) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "ngx_media_system_disk_delete,link the dir:[%V] fail.",&path);
            curNode = curNode->next;
            continue;
        }
        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "ngx_media_system_disk_delete,the:[%V] is not a dir.",&path);
            curNode = curNode->next;
            continue;
        }

        childNode = curNode->children;

        if(NULL == childNode) {
             /* walk the delete dir */
            if (ngx_walk_tree(&tree, &path) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "ngx_media_system_disk_delete,walk tree:[%V] fail.",&path);
            }
        }
        else {
            while(NULL != childNode) {
                if (0 == xmlStrcmp(childNode->name, BAD_CAST SYSTEM_XML_DISK_FILE)) {
                    attr_name = xmlGetProp(childNode,BAD_CAST COMMON_XML_NAME);
                    if(NULL == attr_name) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_system_disk_delete,file name not found.");
                        childNode = childNode->next;
                        continue;
                    }
                    sub_path.len = path.len + ngx_strlen(attr_name);
                    size = sub_path.len + 1;
                    sub_path.data = ngx_pcalloc(r->pool, size+1);

                    last = ngx_snprintf(sub_path.data,size, "%V/%s", &path,attr_name);
                    *last = '\0';
                    ngx_log_debug1(NGX_LOG_DEBUG_HTTP,  r->connection->log, 0,
                                          "ngx_media_system_disk_delete,delete file:[%V].",&sub_path);
                    if (ngx_delete_file(sub_path.data) == NGX_FILE_ERROR) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_system_disk_delete,delete file:[%V] fail.",&sub_path);
                    }
                }
                else if (0 == xmlStrcmp(childNode->name, BAD_CAST SYSTEM_XML_DISK_DIR)) {
                    attr_name = xmlGetProp(childNode,BAD_CAST COMMON_XML_NAME);
                    if(NULL == attr_name) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_system_disk_delete,dir name not found.");
                        childNode = childNode->next;
                        continue;
                    }
                    sub_path.len = path.len + ngx_strlen(attr_name);
                    size = sub_path.len + 1;
                    sub_path.data = ngx_pcalloc(r->pool, size+1);

                    last = ngx_snprintf(sub_path.data,size, "%V/%s", &path,attr_name);
                    *last = '\0';
                    ngx_log_debug1(NGX_LOG_DEBUG_HTTP,  r->connection->log, 0,
                                          "ngx_media_system_disk_delete,delete sub dir:[%V].",&sub_path);
                    if (ngx_walk_tree(&tree, &sub_path) != NGX_OK) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                          "ngx_media_system_disk_delete,walk sub tree:[%V] fail.",&sub_path);
                    }
                }

                childNode = childNode->next;
            }
        }

        curNode = curNode->next;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                              "ngx_media_system_disk_delete,delete file or dir end.");
    if(NULL != Node) {
       disk_node = xmlCopyNodeList(Node);
       xmlAddChild(root_node, disk_node);
    }

    xmlDocDumpFormatMemory(resp_doc, &xmlbuff, &buffersize, 1);

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if(b == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    pbuff = ngx_pcalloc(r->pool, buffersize+1);
    if(pbuff == NULL) {
        xmlFree(xmlbuff);
        xmlFreeDoc(resp_doc);
        return NGX_ERROR;
    }

    last = ngx_copy(pbuff,xmlbuff,buffersize);
    *last = '\0';

    xmlFree(xmlbuff);
    xmlFreeDoc(resp_doc);

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.content_length_n = buffersize;

    out->buf = b;
    out->next = NULL;

    b->pos = pbuff;
    b->last = pbuff + buffersize;

    b->memory = 1;
    b->last_buf = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_media_system_disk_req(ngx_http_request_t *r,xmlDocPtr doc,ngx_chain_t* out)
{
    xmlNodePtr curNode    = NULL;
    xmlChar* attr_value   = NULL;

    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode) {
       return NGX_ERROR;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST COMMON_XML_REQ)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video manage deal operate request,req not found.");
       return NGX_ERROR;
    }
    /* disk node */
    curNode = curNode->children;

    if (xmlStrcmp(curNode->name, BAD_CAST SYSTEM_XML_DISK)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video manage deal operate request,operate not found.");
       return NGX_ERROR;
    }

    attr_value = xmlGetProp(curNode,BAD_CAST COMMON_XML_COMMAND);
    if(NULL == attr_value) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video manage deal operate request,type not found.");
        return NGX_ERROR;
    }


    if (!xmlStrcmp(attr_value, BAD_CAST SYSTEM_COMMAND_DISK_STAT)) {
        return ngx_media_system_disk_stat(r,out);
    }
    else if (!xmlStrcmp(attr_value, BAD_CAST SYSTEM_COMMAND_DISK_LIST)) {
        return ngx_media_system_disk_list(r,curNode,out);
    }
    else if (!xmlStrcmp(attr_value, BAD_CAST SYSTEM_COMMAND_DISK_DEL)) {
        return ngx_media_system_disk_delete(r,curNode,out);
    }
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ngx http vido manage request operate type is invalid.");
    return NGX_ERROR;
}

static ngx_int_t
ngx_media_system_deal_xml_req(ngx_http_request_t *r,const char* req_xml,ngx_chain_t* out)
{
    xmlDocPtr doc;
    int ret = 0;
    xmlNodePtr curNode  = NULL;

    xmlKeepBlanksDefault(0);
    doc = xmlReadDoc((const xmlChar *)req_xml,"msg_req.xml",NULL,0);
    if(NULL == doc) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video system deal the xml fail.");
        return NGX_ERROR;
    }


    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video system get the xml root fail.");
        xmlFreeDoc(doc);
        return NGX_ERROR;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST COMMON_XML_REQ)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video system find the xml req node fail.");
        xmlFreeDoc(doc);
        return NGX_ERROR;
    }

    curNode = curNode->children;
    if (!xmlStrcmp(curNode->name, BAD_CAST SYSTEM_XML_DISK)) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                          "http video system find the xml disk node :%s.",curNode->name);
        ret = ngx_media_system_disk_req(r,doc,out);

    }
    else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "http video system unknow xml node :%s.",curNode->name);
        xmlFreeDoc(doc);
        return NGX_ERROR;
    }

    xmlFreeDoc(doc);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                          "ngx http video task deal xml req,ret:%d.",ret);
    return ret;
}
static void
ngx_media_system_recv_request(ngx_http_request_t *r)
{
    int ret;
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
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video manager ,alloc the buf fail.");
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
    ret = ngx_media_system_deal_xml_req(r,(const char * )req,&out);
    if(NGX_OK != ret){
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video manager ,deal xml request fail,xml:'%s'.",req,&out);
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
    r->headers_out.status = NGX_HTTP_OK;
    r->keepalive   = 0;

    if(NULL == out.buf) {
        r->header_only = 1;
        ngx_http_send_header(r);
        ngx_http_finalize_request(r, NGX_DONE);
    }
    else {
        r->header_only = 0;
        ngx_http_send_header(r);
        ngx_http_finalize_request(r, ngx_http_output_filter(r, &out));
    }
    return ;
}


