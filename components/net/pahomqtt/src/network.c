/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-09 21:30:54
 * @LastEditTime: 2020-06-05 17:17:48
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_timer.h"
#include "platform_memory.h"
#include "nettype_tcp.h"

#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
#include "nettype_tls.h"
#endif

int network_read(network_t *n, unsigned char *buf, int len, int timeout)
{
#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
    if (n->channel)
        return nettype_tls_read(n, buf, len, timeout);
#endif
    return nettype_tcp_read(n, buf, len, timeout);
}

int network_write(network_t *n, unsigned char *buf, int len, int timeout)
{
#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
    if (n->channel)
        return nettype_tls_write(n, buf, len, timeout);
#endif
    return nettype_tcp_write(n, buf, len, timeout);
}

int network_connect(network_t *n)
{
#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
    if (n->channel)
        return nettype_tls_connect(n);
#endif
    return nettype_tcp_connect(n);

}

void network_disconnect(network_t *n)
{
#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
    if (n->channel)
        nettype_tls_disconnect(n);
    else
#endif
        nettype_tcp_disconnect(n);
}

int network_init(network_t *n, const char *host, const char *port, const char *ca, const char *cert, const char *pk, int ssl_en, const char *sni_host, char **alpn_list)
{
    if (NULL == n)
        RETURN_ERROR(KAWAII_MQTT_NULL_VALUE_ERROR);

    n->socket = -1;
    n->host = host;
    n->port = port;

#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
    n->channel = ssl_en;
    n->sni_host = sni_host;
    n->alpn_list = alpn_list;

    if (NULL != ca) {
        network_set_ca(n, ca);
        if(cert != NULL && pk != NULL){
            n->client_cert = cert;
            n->client_cert_len = strlen(cert);
            n->client_pk = pk;
            n->client_pk_len = strlen(pk);
        }
    }
#endif
    RETURN_ERROR(KAWAII_MQTT_SUCCESS_ERROR);
}

void network_release(network_t* n)
{
    if (n->socket >= 0)
        network_disconnect(n);

}

void network_set_channel(network_t *n, int channel)
{
#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
    n->channel = channel;
#endif
}

int network_set_ca(network_t *n, const char *ca)
{
#ifdef KAWAII_MQTT_NETWORK_TYPE_TLS
    if ((NULL == n) || (NULL == ca))
        RETURN_ERROR(KAWAII_MQTT_NULL_VALUE_ERROR);
    
    n->ca_crt = ca;
    n->ca_crt_len = strlen(ca);
    n->channel = NETWORK_CHANNEL_TLS;
    n->timeout_ms = KAWAII_MQTT_TLS_HANDSHAKE_TIMEOUT;
#endif
    RETURN_ERROR(KAWAII_MQTT_SUCCESS_ERROR);
}

int network_set_host_port(network_t* n, char *host, char *port)
{
    if (!(n && host && port))
        RETURN_ERROR(KAWAII_MQTT_NULL_VALUE_ERROR);

    n->host = host;
    n->port = port;

    RETURN_ERROR(KAWAII_MQTT_SUCCESS_ERROR);
}

int network_sockerrno(network_t *n)
{
    int sockerrno = 0;
    sockerrno = platform_net_socket_errno(n->socket);
    if (sockerrno < 0) {
        RETURN_ERROR(KAWAII_MQTT_CONNECT_FAILED_ERROR);
    } else {
        RETURN_ERROR(platform_net_sockerrno_map(sockerrno));
    }
}
