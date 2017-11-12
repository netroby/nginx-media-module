/******************************************************************************

                 Copyright(C), 2016-2020,H.kernel.

 File Name  : ngx_media_license_module.c
 Version    : V 1.0.0
 Date       : 2017-08-25
 Author     : hexin H.kernel
 Modify     :
            1.2017-08-25: create
******************************************************************************/
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include <ngx_log.h>
#include <ngx_http.h>
#include <ngx_files.h>
#include <ngx_md5.h>

#include <net/if.h>           /* for ifconf */
#include <linux/sockios.h>    /* for net status mask */
#include <netinet/in.h>       /* for sockaddr_in */
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdio.h>
#include "ngx_media_license_module.h"
#include "ngx_media_include.h"



#define LICENSE_TASK_COUNT_DEFAULT      2   /* default the task count */
#define LICENSE_RTMP_CHANNEL_DEFAULT    10  /* default the RTMP channel */
#define LICENSE_RTSP_CHANNEL_DEFAULT    10  /* default the RTSP channel */
#define LICENSE_HLS_CHANNEL_DEFAULT     10  /* default the HLS channel */

#define LICENSE_FILE_NAME              "license.xml"
#define LICENSE_FILE_PATH_LEN          512


#define LICENSE_MAX_NETCARD_NUM        16

#define LICENSE_CONFUSE_CODE            "ALL@MEDIA#0608"
#define LICENSE_CONFUSE_LEN            20

#define LICENSE_HOST_MAC_MAX           512
#define LICENSE_HOST_MAC_LEN           20    /*00:50:56:8C:31:22*/
#define LICENSE_ABIIITY_STR_LEN        20    /*1000:1000:1000:1000*/


#define LICENSE_HASH_KEY_MAX           (LICENSE_HOST_MAC_MAX*LICENSE_HOST_MAC_LEN+LICENSE_ABIIITY_STR_LEN+LICENSE_CONFUSE_LEN)
#define LICENSE_HASH_CODE_LEN          16
#define LICENSE_HASH_CODE_STR_LEN      32


typedef struct {
    ngx_uint_t                     taskcount;
    ngx_uint_t                     rtmp_channel;
    ngx_uint_t                     hls_channel;
    ngx_uint_t                     rtsp_channel;
} ngx_media_license_info_t;


static ngx_media_license_info_t *g_license_info = NULL;


static ngx_int_t ngx_media_license_init_worker(ngx_cycle_t *cycle);
static void      ngx_media_license_exit_worker(ngx_cycle_t *cycle);

static ngx_http_module_t  ngx_media_license_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    NULL,                                   /* create location configuration */
    NULL,                                   /* merge location configuration */
};


ngx_module_t  ngx_media_license_module = {
    NGX_MODULE_V1,
    &ngx_media_license_module_ctx,     /* module context */
    NULL,                                   /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    ngx_media_license_init_worker,     /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    ngx_media_license_exit_worker,     /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_media_license_mac(ngx_cycle_t *cycle,ngx_array_t* maclist)
{
    int32_t               isock = -1;
    struct ifconf         stIfc;
    struct ifreq         *pIfr = NULL;
    struct ifreq          stArrayIfr[LICENSE_MAX_NETCARD_NUM];
    struct sockaddr_in   *pAddr = NULL;
    ngx_uint_t            i = 0,j = 0;
    u_char                szMac[LICENSE_HOST_MAC_LEN];
    u_char                szBigMac[LICENSE_HOST_MAC_LEN];
    ngx_str_t            *mac  = NULL;
    u_char               *last = NULL;
    if(NULL == maclist) {
        return NGX_ERROR;
    }

    ethtool_cmd_t stEcmd;

    isock = socket(AF_INET, SOCK_DGRAM, 0);
    if(0 > isock)
    {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"Get netcard info failed. Create socket failed.");
        return NGX_ERROR;
    }

    memset(&stIfc, 0, sizeof(stIfc));
    memset(&stEcmd, 0, sizeof(ethtool_cmd_t));
    memset(stArrayIfr, 0, sizeof(stArrayIfr));

    stEcmd.cmd = ETHTOOL_GSET;
    stIfc.ifc_len = sizeof(struct ifreq) * LICENSE_MAX_NETCARD_NUM;
    stIfc.ifc_buf = (char *) stArrayIfr;

    if (ioctl(isock, SIOCGIFCONF, &stIfc) < 0)
    {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                "Get all active netcard failed.ioctl <SIOCGIFCONF> failed.");
        close (isock);
        return NGX_ERROR;
    }

    pIfr = stIfc.ifc_req;

    for (i = 0; i < (ngx_uint_t) stIfc.ifc_len; i += sizeof(struct ifreq))
    {
        if (NGX_OK != ioctl(isock, SIOCGIFADDR, pIfr))
        {
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"get netcard ifconfig failed,.");

            pIfr++;
            continue;
        }

        pAddr = (struct sockaddr_in *) (void*)&pIfr->ifr_hwaddr;

        if (0 == strncmp(pIfr->ifr_name, "lo", ngx_strlen("lo")))
        {
            ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,"the netcard is lo.");
            pIfr++;
            continue;
        }



        if (NGX_OK != ioctl(isock, SIOCGIFHWADDR, pIfr))
        {
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                "get netcard hardaddr failed,Name[%s].",pIfr->ifr_name);

            pIfr++;
            continue;
        }

        last = ngx_snprintf(szMac,LICENSE_HOST_MAC_LEN,"%02xd:%02xd:%02xd:%02xd:%02xd:%02xd",
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[0],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[1],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[2],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[3],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[4],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[5]);
        *last = '\0';

        last = ngx_snprintf(szBigMac,LICENSE_HOST_MAC_LEN,"%02Xd:%02Xd:%02Xd:%02Xd:%02Xd:%02Xd",
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[0],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[1],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[2],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[3],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[4],
                                                 (unsigned char)pIfr->ifr_hwaddr.sa_data[5]);
        *last = '\0';


        mac = maclist->elts;
        for(j = 0;j < maclist->nelts;j++) {
            if(0 == ngx_memcmp(szMac, mac[j].data,mac[j].len)) {
                close (isock);
                ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"check the license mac success.");
                return NGX_OK;
            }
            if(0 == ngx_memcmp(szBigMac, mac[j].data,mac[j].len)) {
                close (isock);
                ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"check the license mac success.");
                return NGX_OK;
            }
        }

        pIfr++;
    }
    close (isock);
    ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"check the license mac fail.");
    return NGX_ERROR;
}


static ngx_int_t
ngx_media_license_check(ngx_cycle_t *cycle,u_char* hash,ngx_array_t* maclist,
                             ngx_uint_t task_count,ngx_uint_t rtmp_channel,
                             ngx_uint_t rtsp_channel,ngx_uint_t hls_channel)
{
    ngx_uint_t            i = 0,j = 0;
    u_char                szKey[LICENSE_HASH_KEY_MAX];
    u_char                szCode[LICENSE_HASH_CODE_LEN];
    ngx_md5_t             md5;
    u_char               *last = NULL;
    ngx_str_t            *mac  = NULL;
    if(NULL == hash) {
        return NGX_ERROR;
    }

    ngx_memzero(szKey, LICENSE_HASH_KEY_MAX);
    ngx_memzero(szCode, LICENSE_HASH_CODE_LEN);

    /* add ability and confuse code */
    last = ngx_snprintf(szKey,LICENSE_HASH_KEY_MAX,"%d:%d:%d:%d:%s",
                                                 task_count,rtmp_channel,rtsp_channel,hls_channel,
                                                 LICENSE_CONFUSE_CODE);

    /* add the mac list info */
    if (NULL != maclist)
    {
        mac = maclist->elts;
        for(i = 0;i < maclist->nelts;i++) {
            last = ngx_snprintf(last,LICENSE_HOST_MAC_LEN,":%V",&mac[i]);
        }
    }
    *last = '\0';

    ngx_md5_init(&md5);
    ngx_md5_update(&md5, szKey, ngx_strlen(szKey));
    ngx_md5_final(szCode, &md5);

    ngx_memzero(szKey, LICENSE_HASH_KEY_MAX);
    for(j =0 ; j < LICENSE_HASH_CODE_LEN;j++) {
        last = ngx_snprintf((u_char*)&szKey[j*2],3,"%02Xd",szCode[j]);
    }
    *last = '\0';

    if( 0 == ngx_memcmp(szKey, hash, LICENSE_HASH_CODE_STR_LEN)) {
        ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,"check the license success.");
        return NGX_OK;
    }
    ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"check the license fail,%s.",szKey);
    //ngx_log_error(NGX_LOG_WARN, cycle->log, 0,"check the license fail.");
    return NGX_ERROR;
}


static void
ngx_media_license_init(ngx_cycle_t *cycle,ngx_media_license_info_t* license)
{
    ngx_file_info_t                fi;
    ngx_int_t                      rc;
    ngx_uint_t                     i;
    xmlDocPtr                      doc;
    xmlNodePtr                     curNode      = NULL;
    xmlNodePtr                     hostNode     = NULL;
    xmlChar                       *attr_value   = NULL;
    u_char                        *pHash        = NULL;
    u_char                        *last         = NULL;
    ngx_array_t                   *hostArray    = NULL;
    ngx_str_t                     *mac          = NULL;
    ngx_uint_t                     task_count   = LICENSE_TASK_COUNT_DEFAULT;
    ngx_uint_t                     rtmp_channel = LICENSE_RTMP_CHANNEL_DEFAULT;
    ngx_uint_t                     rtsp_channel = LICENSE_RTSP_CHANNEL_DEFAULT;
    ngx_uint_t                     hls_channel  = LICENSE_HLS_CHANNEL_DEFAULT;
    ngx_uint_t                     count = 0;
    u_char                         licfile[LICENSE_FILE_PATH_LEN];

    ngx_memzero(licfile, LICENSE_FILE_PATH_LEN);

    last = ngx_snprintf(licfile, LICENSE_FILE_PATH_LEN,"%V/%s",&cycle->conf_prefix,LICENSE_FILE_NAME);
    *last = '\0';

    /*1.check the license file exsit */
    rc = ngx_file_info(licfile, &fi);
    if (rc != NGX_FILE_ERROR)
    {
        rc = ngx_is_file(&fi);
        if(!rc)
        {
            /* the licens file is not exist, so use default value */
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "the license path:[%s] is not file.",licfile);
            return;
        }
    }
    else
    {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "the license file:[%s] is not exist.",licfile);
        return;
    }

    /*2.read the licens file info */
    xmlKeepBlanksDefault(0);
    doc = xmlParseFile((char*)&licfile[0]);
    if(NULL == doc)
    {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "parse the license xml file fail.");
        return;
    }
    curNode = xmlDocGetRootElement(doc);
    if (NULL == curNode)
    {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "license file get the xml root fail.");
        xmlFreeDoc(doc);
        return;
    }

    if (xmlStrcmp(curNode->name, BAD_CAST "license"))
    {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "license file find the xml license node fail.");
        xmlFreeDoc(doc);
        return;
    }

    hostArray =  ngx_array_create(cycle->pool, LICENSE_HOST_MAC_MAX,
                                                sizeof(ngx_str_t));
    if (NULL == hostArray)
    {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "license create host array fail.");
        xmlFreeDoc(doc);
        return;
    }

    curNode = curNode->children;
    while(NULL != curNode) {
        if (!xmlStrcmp(curNode->name, BAD_CAST "hostlist"))  {
            hostNode = curNode->children;
            while(NULL != hostNode) {
                attr_value = xmlGetProp(hostNode,BAD_CAST "mac");
                if(NULL == attr_value) {
                    hostNode = hostNode->next;
                    continue;
                }
                mac = ngx_array_push(hostArray);
                if (mac == NULL) {
                    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,"license push mac info fail.");
                    break;
                }
                mac->len = ngx_strlen(attr_value);
                mac->data = ngx_pcalloc(cycle->pool,mac->len);
                ngx_memcpy(mac->data, attr_value, mac->len);

                hostNode = hostNode->next;
            }
        }
        else if (!xmlStrcmp(curNode->name, BAD_CAST "ability"))  {
            attr_value = xmlGetProp(curNode,BAD_CAST "rtmp");
            if(NULL != attr_value) {
                count = ngx_atoi((u_char*)attr_value,ngx_strlen(attr_value));
                if(count > rtmp_channel)
                {
                    rtmp_channel = count;
                }
            }
            attr_value = xmlGetProp(curNode,BAD_CAST "rtsp");
            if(NULL != attr_value) {
                count = ngx_atoi((u_char*)attr_value,ngx_strlen(attr_value));
                if(count > rtsp_channel)
                {
                    rtsp_channel = count;
                }
            }
            attr_value = xmlGetProp(curNode,BAD_CAST "hls");
            if(NULL != attr_value) {
                count = ngx_atoi((u_char*)attr_value,ngx_strlen(attr_value));
                if(count > hls_channel)
                {
                    hls_channel = count;
                }
            }
            attr_value = xmlGetProp(curNode,BAD_CAST "task");
            if(NULL != attr_value) {
                count = ngx_atoi((u_char*)attr_value,ngx_strlen(attr_value));
                if(count > task_count)
                {
                    task_count = count;
                }
            }
        }
        else if (!xmlStrcmp(curNode->name, BAD_CAST "hash"))  {
            pHash = xmlNodeGetContent(curNode);
        }
        curNode = curNode->next;
    }

    /*2.check hash code for license */
    if((NULL != pHash)
        &&(NGX_OK == ngx_media_license_check(cycle,pHash,hostArray,task_count,rtmp_channel,rtsp_channel,hls_channel))
        &&(NGX_OK == ngx_media_license_mac(cycle,hostArray))) {
        license->hls_channel = hls_channel;
        license->rtsp_channel = rtsp_channel;
        license->rtmp_channel = rtmp_channel;
        license->taskcount = task_count;
        ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
                          "check the license success, task:[%d],rtmp:[%d],rtsp:[%d],hls:[%d].",
                          license->taskcount,
                          license->rtmp_channel,
                          license->rtsp_channel,
                          license->hls_channel);
    }
    else {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "check the license fail,so use default task:[%d],rtmp:[%d],rtsp:[%d],hls:[%d].",
                          license->taskcount,
                          license->rtmp_channel,
                          license->rtsp_channel,
                          license->hls_channel);
    }

    if (NULL != hostArray)
    {
        mac = hostArray->elts;
        for(i = 0;i < hostArray->nelts;i++) {
            ngx_pfree(cycle->pool,mac[i].data);
            mac[i].len = 0;
        }
        ngx_array_destroy(hostArray);
        hostArray = NULL;
    }
    xmlFreeDoc(doc);
    return;
}


static ngx_int_t
ngx_media_license_init_worker(ngx_cycle_t *cycle)
{
    if(NULL != g_license_info)
    {
        return NGX_OK;
    }

    g_license_info = ngx_pcalloc(cycle->pool, sizeof(ngx_media_license_info_t));
    if (g_license_info == NULL)
    {
        return NGX_ERROR;
    }
    g_license_info->taskcount      = LICENSE_TASK_COUNT_DEFAULT;
    g_license_info->rtmp_channel   = LICENSE_RTMP_CHANNEL_DEFAULT;
    g_license_info->rtsp_channel   = LICENSE_RTSP_CHANNEL_DEFAULT;
    g_license_info->hls_channel    = LICENSE_HLS_CHANNEL_DEFAULT;

    ngx_media_license_init(cycle,g_license_info);

    return NGX_OK;
}
static void
ngx_media_license_exit_worker(ngx_cycle_t *cycle)
{
    ngx_pfree(cycle->pool,g_license_info);
    g_license_info = NULL;
    return;
}

ngx_uint_t
ngx_media_license_task_count()
{
    if(NULL == g_license_info) {
        return LICENSE_TASK_COUNT_DEFAULT;
    }
    return g_license_info->taskcount;
}
ngx_uint_t
ngx_media_license_rtmp_channle()
{
    if(NULL == g_license_info) {
        return LICENSE_RTMP_CHANNEL_DEFAULT;
    }
    return g_license_info->rtmp_channel;
}
ngx_uint_t
ngx_media_license_rtsp_channle()
{
    if(NULL == g_license_info) {
        return LICENSE_RTSP_CHANNEL_DEFAULT;
    }
    return g_license_info->rtsp_channel;
}
ngx_uint_t
ngx_media_license_hls_channle()
{
    if(NULL == g_license_info) {
        return LICENSE_HLS_CHANNEL_DEFAULT;
    }
    return g_license_info->hls_channel;
}


