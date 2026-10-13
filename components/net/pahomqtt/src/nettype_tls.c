/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2020-01-11 19:45:35
 * @LastEditTime: 2023-11-22 18:11:53
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "nettype_tls.h"
#include "platform_net_socket.h"
#include "platform_memory.h"
#include "platform_timer.h"
#include "random.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lwip/sockets.h"

#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS

#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"

#if defined(MBEDTLS_X509_CRT_PARSE_C)
static int server_certificate_verify(void *hostname, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    if (0 != *flags)
        KAWAII_MQTT_LOG_E("%s:%d %s()... server_certificate_verify failed returned 0x%04x\n", __FILE__, __LINE__, __FUNCTION__, *flags);
    
    return 0;
}
#endif

static int nettype_tls_entropy_source(void *data, uint8_t *output, size_t len, size_t *out_len)
{
    uint32_t seed;
    (void) data;
    seed = random_number();

    if (len > sizeof(seed)) {
        len = sizeof(seed);
    }

    memcpy(output, &seed, len);
    *out_len = len;

    return 0;
}

static int nettype_tls_deinit(network_t *n){
    nettype_tls_params_t *nettype_tls_params = (nettype_tls_params_t *) n->nettype_tls_params;
    if(nettype_tls_params == NULL){
        return 0;
    }
    mbedtls_net_free(&(nettype_tls_params->socket_fd));
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt_free(&(nettype_tls_params->client_cert));
    mbedtls_x509_crt_free(&(nettype_tls_params->ca_cert));
    mbedtls_pk_free(&(nettype_tls_params->private_key));
#endif
    mbedtls_ssl_free(&(nettype_tls_params->ssl));
    mbedtls_ssl_config_free(&(nettype_tls_params->ssl_conf));
    mbedtls_ctr_drbg_free(&(nettype_tls_params->ctr_drbg));
    mbedtls_entropy_free(&(nettype_tls_params->entropy));

    platform_memory_free(nettype_tls_params);
    n->nettype_tls_params = NULL;
    KAWAII_MQTT_LOG_E("##### nettype_tls_disconnect done");
    return 0;
}

#define NETTYPE_TLS_HOSTBUF_LEN 256
#define MAX_IP_CNT  (64)
#define MAX_IP_LEN (64)
#define INET_ADDRSTRLEN (64)

extern struct hostent *gethostbyname_safe(const char *name, void *host_buf, int len);

static int nettype_tls_dns_lookup(const char *host,  char ip[MAX_IP_CNT][MAX_IP_LEN], int *ip_cnt)
{
    if ((host == NULL) || (ip == NULL) || (ip_cnt == NULL)) {
        return -1;
    }
    int i = 0;
    int retval = 0;
    char **pptr;
    char host_buf[NETTYPE_TLS_HOSTBUF_LEN] = {0};
    char ip_str[INET_ADDRSTRLEN];

    struct hostent *hptr = gethostbyname_safe(host, host_buf, sizeof(host_buf));
    if ((hptr == NULL) || (hptr->h_addrtype != AF_INET) || (hptr->h_addr_list == NULL) ||
        (hptr->h_addr_list[0] == NULL)) {
        if (hptr != NULL) {
            free(hptr);
        }
        return -1;
    }

    if (ip_cnt) {
        *ip_cnt = 0;
    }

    switch(hptr->h_addrtype) {
        case AF_INET: 
            pptr = hptr->h_addr_list; //IP地址数组
            for(i = 0; *pptr != NULL && i < MAX_IP_CNT; pptr++, i++) {
                inet_ntop(hptr->h_addrtype, *pptr, ip_str, sizeof(ip_str));
                KAWAII_MQTT_LOG_I("i=[%d]  IP addr: %s\n",i, ip_str);
                snprintf(ip[i], MAX_IP_LEN, "%s", ip_str);
                if (ip_cnt) {
                    *ip_cnt += 1;
                }
            }      
            retval = 0;
            break;

        default:
            KAWAII_MQTT_LOG_I("unknown address type\n");
            retval = -2;
            break;
    }  
    free(hptr);
    return retval;
}

static int nettype_tls_socket_connect(network_t *n, nettype_tls_params_t *params)
{
    char ip[MAX_IP_CNT][MAX_IP_LEN];
	int ipcnt = 0;

    if (nettype_tls_dns_lookup(n->host, ip, &ipcnt) != 0) {
        KAWAII_MQTT_LOG_E("dns resolve failed: %s", n->host);
        return MBEDTLS_ERR_NET_UNKNOWN_HOST;
    }

    char *endptr = NULL;
    unsigned long port_num = strtoul(n->port, &endptr, 10);
    if ((endptr == n->port) || (port_num == 0) || (port_num > 65535UL)) {
        KAWAII_MQTT_LOG_E("invalid tls port: %s", n->port);
        return MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        KAWAII_MQTT_LOG_E("socket create failed, errno=%d", getsockerrno(sock));
        return MBEDTLS_ERR_NET_SOCKET_FAILED;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port_num);
    server_addr.sin_addr.s_addr = inet_addr(ip[0]);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        KAWAII_MQTT_LOG_E("socket connect failed, errno=%d", getsockerrno(sock));
        closesocket(sock);
        return MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    if (n->timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = n->timeout_ms / 1000;
        tv.tv_usec = (n->timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    params->socket_fd.fd = sock;
    return 0;
}


static int _ssl_net_send( void *ctx, const unsigned char *buf, size_t len )
{
    int ret;
    int fd = ((mbedtls_net_context *) ctx)->fd;

    if( fd < 0 )
        return( MBEDTLS_ERR_NET_INVALID_CONTEXT );
    ret = (int) send( fd, buf, len , 0);
    if( ret < 0 )
    {

        return( MBEDTLS_ERR_NET_SEND_FAILED );
    }

    return( ret );
}

static int _ssl_net_recv( void *ctx, unsigned char *buf, size_t len )
{
    int ret;
    int fd = ((mbedtls_net_context *) ctx)->fd;

    if( fd < 0 )
        return( MBEDTLS_ERR_NET_INVALID_CONTEXT );
    
    ret = (int) recv( fd, buf, len,0 );
    if( ret == -1 && errno == EAGAIN )
    {
        return( MBEDTLS_ERR_SSL_WANT_READ );
    }

    return( ret );
}

OSI_UNUSED static int _ssl_net_recv_timeout( void *ctx, unsigned char *buf, size_t len , uint32_t timeout)
{
    int ret;
    int fd = ((mbedtls_net_context *) ctx)->fd;

    if( fd < 0 )
        return( MBEDTLS_ERR_NET_INVALID_CONTEXT );
    while (1)
    {
        ret = (int) recv( fd, buf, len,0 );
        if( ret < 0 )
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if(timeout <= 0)
                {
                    return( ret );
                }
                timeout -= 50;
                osiThreadSleep(50);
                continue;
            }
            return( MBEDTLS_ERR_NET_RECV_FAILED );
        } else {
            break;
        }

    }
    

    return( ret );
}

static int nettype_tls_init(network_t* n, nettype_tls_params_t* nettype_tls_params)
{
    int rc = KAWAII_MQTT_SUCCESS_ERROR;

#if defined(MBEDTLS_PLATFORM_MEMORY)
    mbedtls_platform_set_calloc_free(platform_memory_calloc, platform_memory_free);
#endif

    mbedtls_net_init(&(nettype_tls_params->socket_fd));
    mbedtls_ssl_init(&(nettype_tls_params->ssl));
    mbedtls_ssl_config_init(&(nettype_tls_params->ssl_conf));
    mbedtls_ctr_drbg_init(&(nettype_tls_params->ctr_drbg));
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt_init(&(nettype_tls_params->ca_cert));
    mbedtls_x509_crt_init(&(nettype_tls_params->client_cert));
    mbedtls_pk_init(&(nettype_tls_params->private_key));
#endif
    
    mbedtls_entropy_init(&(nettype_tls_params->entropy));
    mbedtls_entropy_add_source(&(nettype_tls_params->entropy), nettype_tls_entropy_source, NULL, MBEDTLS_ENTROPY_MAX_GATHER, MBEDTLS_ENTROPY_SOURCE_STRONG);
    
    if ((rc = mbedtls_ctr_drbg_seed(&(nettype_tls_params->ctr_drbg), mbedtls_entropy_func,
                                    &(nettype_tls_params->entropy), NULL, 0)) != 0) {
        KAWAII_MQTT_LOG_E("mbedtls_ctr_drbg_seed failed returned 0x%04x", (rc < 0 )? -rc : rc);
        RETURN_ERROR(rc);
    }

    if ((rc = mbedtls_ssl_config_defaults(&(nettype_tls_params->ssl_conf), MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        KAWAII_MQTT_LOG_E("mbedtls_ssl_config_defaults failed returned 0x%04x", (rc < 0 )? -rc : rc);
        RETURN_ERROR(rc);
    }

    mbedtls_ssl_conf_rng(&(nettype_tls_params->ssl_conf), mbedtls_ctr_drbg_random, &(nettype_tls_params->ctr_drbg));

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    if (NULL != n->ca_crt) {
        n->ca_crt_len = strlen(n->ca_crt);

        if (0 != (rc = (mbedtls_x509_crt_parse(&(nettype_tls_params->ca_cert), (unsigned char *)n->ca_crt,
                                          (n->ca_crt_len + 1))))) {
            KAWAII_MQTT_LOG_E("%s:%d %s()... parse ca crt failed returned 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
            RETURN_ERROR(rc);
        }
    }

    if(NULL != n->client_cert && NULL != n->client_pk){
        if (0 != (rc = (mbedtls_x509_crt_parse(&(nettype_tls_params->client_cert), (unsigned char *)n->client_cert,
                                          (n->client_cert_len + 1))))) {
            KAWAII_MQTT_LOG_E("%s:%d %s()... parse client cert failed returned 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
            RETURN_ERROR(rc);
        }

        if (0 != (rc = (mbedtls_pk_parse_key(&(nettype_tls_params->private_key), (unsigned char *)n->client_pk,
                                          (n->client_pk_len + 1), NULL, 0)))) {
            KAWAII_MQTT_LOG_E("%s:%d %s()... parse client private key failed returned 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
            RETURN_ERROR(rc);
        }
    }

    mbedtls_ssl_conf_ca_chain(&(nettype_tls_params->ssl_conf), &(nettype_tls_params->ca_cert), NULL);

    if ((rc = mbedtls_ssl_conf_own_cert(&(nettype_tls_params->ssl_conf),
                                         &(nettype_tls_params->client_cert), &(nettype_tls_params->private_key))) != 0) {
        KAWAII_MQTT_LOG_E("%s:%d %s()... mbedtls_ssl_conf_own_cert failed returned 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
        RETURN_ERROR(rc);
    }
    if(n->sni_host != NULL){
        mbedtls_ssl_conf_verify(&(nettype_tls_params->ssl_conf), server_certificate_verify, (void *)n->sni_host);
    } else {
        mbedtls_ssl_conf_verify(&(nettype_tls_params->ssl_conf), server_certificate_verify, (void *)n->host);
    }

    // mbedtls_ssl_conf_authmode(&(nettype_tls_params->ssl_conf), MBEDTLS_SSL_VERIFY_NONE);
#endif

#if defined(MBEDTLS_SSL_ALPN)
    if( n->alpn_list ) {
        rc = mbedtls_ssl_conf_alpn_protocols(&(nettype_tls_params->ssl_conf), (const char **)n->alpn_list);
        if (rc != 0) {
            KAWAII_MQTT_LOG_E("set ALPN failed 0x%04x", (rc < 0) ? -rc : rc);
            RETURN_ERROR(rc);
        }
    }
#endif
    mbedtls_ssl_conf_authmode(&(nettype_tls_params->ssl_conf), MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_read_timeout(&(nettype_tls_params->ssl_conf), n->timeout_ms);

    if ((rc = mbedtls_ssl_setup(&(nettype_tls_params->ssl), &(nettype_tls_params->ssl_conf))) != 0) {
        KAWAII_MQTT_LOG_E("mbedtls_ssl_setup failed returned 0x%04x", (rc < 0 )? -rc : rc);
        RETURN_ERROR(rc);
    }
    
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    /* Enable SNI to help server pick correct certificate */
    if(n->sni_host != NULL){
        rc = mbedtls_ssl_set_hostname(&(nettype_tls_params->ssl), n->sni_host);
    } else {
        rc = mbedtls_ssl_set_hostname(&(nettype_tls_params->ssl), n->host);
    }
    if (rc != 0) {
        KAWAII_MQTT_LOG_E("%s:%d %s()... mbedtls_ssl_set_hostname failed returned 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
        RETURN_ERROR(rc);
    }
#endif
    mbedtls_ssl_set_bio(&(nettype_tls_params->ssl), &(nettype_tls_params->socket_fd), _ssl_net_send, _ssl_net_recv, NULL);

    RETURN_ERROR(KAWAII_MQTT_SUCCESS_ERROR);
}


int nettype_tls_connect(network_t* n)
{
    int rc;
    if (NULL == n)
        RETURN_ERROR(KAWAII_MQTT_NULL_VALUE_ERROR);

    nettype_tls_params_t *nettype_tls_params = (nettype_tls_params_t *) platform_memory_alloc(sizeof(nettype_tls_params_t));

    if (NULL == nettype_tls_params)
        RETURN_ERROR(KAWAII_MQTT_MEM_NOT_ENOUGH_ERROR);

    n->nettype_tls_params = nettype_tls_params;
    rc = nettype_tls_init(n, nettype_tls_params);
        if (KAWAII_MQTT_SUCCESS_ERROR != rc){
            goto exit;
        }
    
    if ((rc = nettype_tls_socket_connect(n, nettype_tls_params)) != 0) {
        goto exit;
    }

    
        while ((rc = mbedtls_ssl_handshake(&(nettype_tls_params->ssl))) != 0) {
                if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            KAWAII_MQTT_LOG_E("%s:%d %s()...mbedtls handshake failed returned 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
#if defined(MBEDTLS_X509_CRT_PARSE_C)
            if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                KAWAII_MQTT_LOG_E("%s:%d %s()...unable to verify the server's certificate", __FILE__, __LINE__, __FUNCTION__);
            }
#endif      
                goto exit;
        }
    }

    if (0 && (rc = mbedtls_ssl_get_verify_result(&(nettype_tls_params->ssl))) != 0) {
    KAWAII_MQTT_LOG_E("%s:%d %s()...mbedtls_ssl_get_verify_result returned 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
    goto exit;
    }


    RETURN_ERROR(KAWAII_MQTT_SUCCESS_ERROR)

exit:
    nettype_tls_deinit(n);
    // nettype_tls_disconnect(n);
    RETURN_ERROR(rc);
}


void nettype_tls_disconnect(network_t* n) 
{
    int rc = 0;
    if (NULL == n)
        return;
    
    nettype_tls_params_t *nettype_tls_params = (nettype_tls_params_t *) n->nettype_tls_params;
    if(nettype_tls_params == NULL){
        return;
    }

    do {
        rc = mbedtls_ssl_close_notify(&(nettype_tls_params->ssl));
    } while (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
    closesocket(((mbedtls_net_context *)&(nettype_tls_params->socket_fd))->fd);
    nettype_tls_deinit(n);
}

int nettype_tls_write(network_t *n, unsigned char *buf, int len, int timeout)
{
    int rc = 0;
    int write_len = 0;
    platform_timer_t timer;

    if (NULL == n)
        RETURN_ERROR(KAWAII_MQTT_NULL_VALUE_ERROR);
    
    nettype_tls_params_t *nettype_tls_params = (nettype_tls_params_t *) n->nettype_tls_params;

    
    platform_timer_cutdown(&timer, timeout);

    do {
        rc = mbedtls_ssl_write(&(nettype_tls_params->ssl), (unsigned char *)(buf + write_len), len - write_len);

        if (rc > 0) {
            write_len += rc;
        } else if ((rc == 0) || ((rc != MBEDTLS_ERR_SSL_WANT_WRITE) && (rc != MBEDTLS_ERR_SSL_WANT_READ) && (rc != MBEDTLS_ERR_SSL_TIMEOUT))) {
            KAWAII_MQTT_LOG_E("%s:%d %s()... mbedtls_ssl_write failed: 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
            break;
        } 
    } while((!platform_timer_is_expired(&timer)) && (write_len < len));

    return write_len;
}

int nettype_tls_read(network_t *n, unsigned char *buf, int len, int timeout)
{
    int rc = 0;
    int read_len = 0;
    platform_timer_t timer;

    if (NULL == n)
        RETURN_ERROR(KAWAII_MQTT_NULL_VALUE_ERROR);
    
    nettype_tls_params_t *nettype_tls_params = (nettype_tls_params_t *) n->nettype_tls_params;
    
    
    platform_timer_cutdown(&timer, timeout);
    
    do {
        rc = mbedtls_ssl_read(&(nettype_tls_params->ssl), (unsigned char *)(buf + read_len), len - read_len);

        if (rc > 0) {
            read_len += rc;
        } else if ((rc == 0) || ((rc != MBEDTLS_ERR_SSL_WANT_WRITE) && (rc != MBEDTLS_ERR_SSL_WANT_READ) && (rc != MBEDTLS_ERR_SSL_TIMEOUT))) {
            // KAWAII_MQTT_LOG_E("%s:%d %s()... mbedtls_ssl_read failed: 0x%04x", __FILE__, __LINE__, __FUNCTION__, (rc < 0 )? -rc : rc);
            break;
        } 
    } while((!platform_timer_is_expired(&timer)) && (read_len < len));

    return((read_len > 0) ? read_len : rc);
}


#endif /* KAWAII_MQTT_NETWORK_TYPE_TLS */
