/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

******************************************************************************/
#ifndef __H_HTTP_VIDEO_HEAD_H__
#define __H_HTTP_VIDEO_HEAD_H__
#include <net/if.h>       /* for ifconf */
#include <linux/sockios.h>    /* for net status mask */
#include <netinet/in.h>       /* for sockaddr_in */
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdio.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <nginx.h>
/* libxml2 */
#include "libxml2/libxml/parser.h"

/* http video config file */

#define NGX_HTTP_TRANS_TASK     "video_task"
#define NGX_HTTP_TASK_ARGS      "task_args"
#define NGX_HTTP_TASK_MONITOR   "task_monitor"
#define NGX_HTTP_VIDEO_STAT     "video_stat"
#define NGX_HTTP_STAT_PASSWD    "manage_password"
#define NGX_HTTP_STATIC_TASK    "static_task"

#define NGX_MEDIA_LIVE_CONF_FILE_DEFAULT       "../conf/shmcache.conf"

#define NGX_MEDIA_LIVE                         "media_live"
#define NGX_MEDIA_LIVE_ONPLAY                  "media_live_on_play"
#define NGX_MEDIA_LIVE_ONPLAY_DONE             "media_live_on_play_done"
#define NGX_MEDIA_LIVE_PLAY_TIMEOUT            "media_live_play_timeout"
#define NGX_MEDIA_LIVE_SESSION_CACHE           "media_live_session_cache"




#define NGX_HTTP_SCH_ZK_ADDR      "sch_zk_address"
#define NGX_HTTP_SCH_ZK_PATH      "sch_zk_path"
#define NGX_HTTP_SCH_ZK_UPDATE    "sch_zk_update"
#define NGX_HTTP_SCH_ZK_SIGIP     "sch_signal_ip"
#define NGX_HTTP_SCH_ZK_SERIP     "sch_service_ip"
#define NGX_HTTP_SCH_ZK_VPATH     "sch_disk_vpath"

#define NGX_HTTP_SCH_LOAD_CPU     "sch_load_cpu"
#define NGX_HTTP_SCH_LOAD_MEM     "sch_load_mem"
#define NGX_HTTP_SCH_LOAD_NET     "sch_load_net"
#define NGX_HTTP_SCH_LOAD_DISK    "sch_load_disk"


#define NGX_HTTP_SCH_ZK_UPSTREAM  "video_schedule"

#define NGX_HTTP_SYS_MANAGE       "sys_manage"



/* common define */
#define LOCAL_RTMP_URl       "rtmp://127.0.0.1:1935/rtmp"

#define COMMON_XML_REQ          "req"
#define COMMON_XML_RESP         "resp"
#define COMMON_XML_COMMAND      "command"
#define COMMON_XML_NAME         "name"
#define COMMON_XML_UNIT         "unit"
#define COMMON_XML_VALUE        "value"
#define COMMON_XML_SIZE         "size"
#define COMMON_XML_FREE         "free"


#define TASK_STATUS             "status"
#define TASK_COMMAND_INIT       "init"
#define TASK_COMMAND_START      "start"
#define TASK_COMMAND_RUNNING    "running"
#define TASK_COMMAND_STOP       "stop"
#define TASK_COMMAND_UPDATE     "update"

#define TASK_XML_WORKERS        "workers"
#define TASK_XML_WORKER         "worker"
#define TASK_XML_WORKER_ID      "id"
#define TASK_XML_WORKER_MASTER  "master"
#define TASK_XML_WORKER_TYPE    "type"

#define TASK_XML_PARAMS         "params"
#define TASK_XML_PARAM          "param"
#define TASK_XML_PARAM_PATH     "path"

#define TASK_XML_TRIGGERS       "triggers"
#define TASK_XML_TRIGGER        "trigger"
#define TASK_XML_TRIGGER_AFTER  "after"
#define TASK_XML_TRIGGER_WOKER  "worker"
#define TASK_XML_TRIGGER_DELAY  "delay"


#define TASK_XML_STATINFO       "stat_info"
#define TASK_XML_STAT           "stat"





#define SYSTEM_COMMAND_DISK_STAT  "stat"
#define SYSTEM_COMMAND_DISK_LIST  "list"
#define SYSTEM_COMMAND_DISK_DEL   "delete"

#define SYSTEM_XML_DISK         "disk"
#define SYSTEM_XML_DISK_VPATH   "vpath"
#define SYSTEM_XML_DISK_FILE    "file"
#define SYSTEM_XML_DISK_DIR     "dir"



#define TRANS_REQ_ARG_MANAGE    "manager"

#define TRANS_REQ_ARG_STOP      "stop"
#define TRANS_REQ_ARG_DETAIL    "detail"

#define TRANS_REQ_ARG_TASK      "task"
#define TRANS_REQ_ARG_WORKER    "worker"
#define TRANS_REQ_ARG_TYPE      "type"
#define TRANS_REQ_ARG_PASSWD    "password"







#define TRANS_ERROR_HTML_MSG   "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=GBK\"> \
                                </head><body>hi,guy!<br>this is not a web server,You do not have permission to do something<br>\
                                please leave now !thanks<br>---allmedia---</body></html>"
#define TRANS_SUCCESS_HTML_MSG   "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=GBK\"> \
                                    </head><body>hi,guy!<br>success!<br>---allmedia---</body></html>"


#define TASK_CHECK_INTERVAL   5000  //5 second
#define TASK_INVALIED_TIME    30    //30 second

#define ZOOKEEPR_UPDATE_TIME  5000  //30 second

#define TRANS_STRING_MAX_LEN  1024
#define TRANS_STRING_4K_LEN   4096

#define TRANS_VPATH_KV_MAX    128
#define TRANS_STRING_IP       128
#define TRANS_VPATH_MAX       256

#define TASK_ARGS_MAX         256


typedef struct ngx_media_task_s ngx_media_task_t;

struct ngx_media_task_s {
    ngx_str_t                       task_id;
    volatile ngx_int_t              status;
    volatile time_t                 lastreport;
    ngx_int_t                       rep_inter;
    ngx_str_t                       rep_url;
    ngx_url_t                       url;
    ngx_str_t                       xml;
    ngx_list_t                     *arglist;

    ngx_pool_t                     *pool;
    ngx_log_t                      *log;
    ngx_thread_mutex_t              task_mtx;
    ngx_event_t                     time_event;
    ngx_list_t                     *workers;       /* worker list of the task,(ngx_media_worker_ctx_t)*/
    ngx_media_task_t               *next_task;
    ngx_media_task_t               *prev_task;
};

typedef struct {
    ngx_log_t                      *log;
    ngx_media_task_t               *task_head;
    volatile ngx_uint_t             task_count;
    ngx_thread_mutex_t              task_thread_mtx;
} ngx_media_task_ctx_t;


typedef struct
{
    u_int32_t        bond_mode;
    u_int32_t        num_slaves;
    u_int32_t        miimon;
} ifbond_t;

typedef struct
{
    u_int32_t          slave_id; /* Used as an IN param to the BOND_SLAVE_INFO_QUERY ioctl */
    u_char             slave_name[IFNAMSIZ];
    u_char             link;
    u_char             state;
    u_int32_t          link_failure_count;
} ifslave_t;


typedef struct
{
    u_int32_t  cmd;
    u_int32_t  supported;            /* Features this interface supports */
    u_int32_t  advertising;          /* Features this interface advertises */
    u_int16_t  speed;                /* The forced speed, 10Mb, 100Mb, gigabit */
    uint8_t    duplex;               /* Duplex, half or full */
    uint8_t    port;                 /* Which connector port */
    uint8_t    phy_address;
    uint8_t    transceiver;          /* Which tranceiver to use */
    uint8_t    autoneg;              /* Enable or disable autonegotiation */
    u_int32_t  maxtxpkt;             /* Tx pkts before generating tx int32_t */
    u_int32_t  maxrxpkt;             /* Rx pkts before generating rx int32_t */
    u_int32_t  reserved[4];
} __attribute__ ((packed))          ethtool_cmd_t;
#define ETHTOOL_GSET    0x00000001 /* Get settings. */



static ngx_inline u_char *
ngx_video_strrchr(u_char *begin, u_char *last, u_char c)
{
    while (begin < last) {

        if (*last == c) {
            return last;
        }

        last--;
    }

    return NULL;
}


#endif /*__H_HTTP_VIDEO_HEAD_H__*/
