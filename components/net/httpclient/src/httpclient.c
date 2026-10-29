/* Copyright (C) 2012 mbed.org, MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <string.h>
#include <stdio.h>
#include "httpclient.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
// #include "lwip/tcp.h"
// #include "lwip/err.h"
// #include "sys/socket.h"
#include "osi_api.h"

#if HTTPCLIENT_DEBUG == 1
#define HTTPC_LOG RTI_LOG
#else
#define HTTPC_LOG(FMT, ...)  
#endif
#define HTTPS_READ_TIME_OUT_TICK	1000 //ms
#define HTTPS_RETRY_READ_TIMES		3

#define MIN(x,y) (((x)<(y))?(x):(y))
#define MAX(x,y) (((x)>(y))?(x):(y))

static int httpclient_parse_url(const char *url, char *scheme, size_t max_scheme_len, char *host, size_t maxhost_len, int *port, char *path, size_t max_path_len);
static int httpclient_tcp_send_all(int sock_fd, char *data, int length);
static int _httpTcp_conn(httpclient_t *client, char *host);
static int httpclient_recv(httpclient_t *client, char *buf, int min_len, int max_len, int *p_read_len);
static int httpclient_retrieve_content(httpclient_t *client, char *data, int len, httpclient_data_t *client_data);
static int httpclient_response_parse(httpclient_t *client, char *data, int len, httpclient_data_t *client_data);

static void httpclient_base64enc(char *out, const char *in)
{
    const char code[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=" ;
    int i = 0, x = 0, l = 0;

    for (; *in; in++) {
        x = x << 8 | *in;
        for (l += 8; l >= 6; l -= 6) {
            out[i++] = code[(x >> (l - 6)) & 0x3f];
        }
    }
    if (l > 0) {
        x <<= 6 - l;
        out[i++] = code[x & 0x3f];
    }
    for (; i % 4;) {
        out[i++] = '=';
    }
    out[i] = '\0' ;
}

static int _httpTcp_conn(httpclient_t *client, char *host){
    struct addrinfo hints, *addr_list, *cur;
    int ret = 0;
    char port[10] = {0};
    
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    snprintf(port, sizeof(port), "%d", client->remote_port) ;
    if ( getaddrinfo( host, port , &hints, &addr_list ) != 0 ) {
        HTTPC_LOG("\ngetaddrinfo != 0, return HTTPCLIENT_UNRESOLVED_DNS\n");
        return HTTPCLIENT_UNRESOLVED_DNS;
    }

    /* Try the sockaddrs until a connection succeeds */
    ret = HTTPCLIENT_UNRESOLVED_DNS;
    for ( cur = addr_list; cur != NULL; cur = cur->ai_next ) {
        client->socket = (int) socket( cur->ai_family, cur->ai_socktype,
            cur->ai_protocol );
        if ( client->socket < 0 ) {
            ret = HTTPCLIENT_ERROR_CONN;
            continue;
        }

        struct timeval con_tv;
        con_tv.tv_sec = 15;
        con_tv.tv_usec = 0;
        setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, &con_tv, sizeof(con_tv));
        setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &con_tv, sizeof(con_tv));

        if ( connect( client->socket, cur->ai_addr, (int)cur->ai_addrlen ) == 0 ) {
            ret = 0;
            break;
        }

        closesocket( client->socket );
        ret = HTTPCLIENT_ERROR_CONN;
    }

    freeaddrinfo( addr_list );

    return ret;
}

static int _ssl_random(void *p_rng, unsigned char *output, size_t output_len)
{
    uint32_t rnglen = output_len;
    uint8_t rngoffset = 0;

    while (rnglen > 0)
    {
        *(output + rngoffset) = (((unsigned int)rand() << 16) + rand());
        rngoffset++;
        rnglen--;
    }
    return 0;
}


static void _ssl_debug(void *ctx, int level, const char *file, int line, const char *str)
{
    ((void)level);
    if (NULL != ctx)
    {
        HTTPC_LOG("%s\n", str);
    }
}

static httpclient_ssl_t *_sslContextInit(void){
    httpclient_ssl_t *ssl = malloc(sizeof(httpclient_ssl_t));
    if(ssl == NULL){
        return NULL;
    }
    mbedtls_net_init(&(ssl->net_ctx));
    mbedtls_ssl_init(&(ssl->ssl_ctx));
    mbedtls_ssl_config_init(&(ssl->ssl_conf));
    mbedtls_ctr_drbg_init(&(ssl->ctr_drbg));
    mbedtls_x509_crt_init(&(ssl->cacert));
    mbedtls_x509_crt_init(&(ssl->clicert));
    mbedtls_pk_init(&(ssl->pkey));
    mbedtls_entropy_init(&(ssl->entropy));

    return ssl;
}

static void _sslContextfree(httpclient_ssl_t *ssl){
    mbedtls_ssl_close_notify(&(ssl->ssl_ctx));
    mbedtls_net_free(&(ssl->net_ctx));
    mbedtls_ssl_free(&(ssl->ssl_ctx));
    mbedtls_ssl_config_free(&(ssl->ssl_conf));
    mbedtls_x509_crt_free(&(ssl->cacert));
    mbedtls_x509_crt_free(&(ssl->clicert));
    mbedtls_pk_free(&(ssl->pkey));
    free(ssl);
}

int _serverCertificateVerify(void *hostname, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    return *flags;
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

/*
 * Initiate a TCP connection with host:port and the given protocol
 */
int _HttpNetConnect( mbedtls_net_context *ctx, const char *host,
                         const char *port, int proto )
{
    int ret;
    struct addrinfo hints, *addr_list, *cur;

    /* Do name resolution with IPv4 only*/
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = proto == MBEDTLS_NET_PROTO_UDP ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = proto == MBEDTLS_NET_PROTO_UDP ? IPPROTO_UDP : IPPROTO_TCP;

    if( getaddrinfo( host, port, &hints, &addr_list ) != 0 )
        return( MBEDTLS_ERR_NET_UNKNOWN_HOST );

    /* Try the sockaddrs until a connection succeeds */
    ret = MBEDTLS_ERR_NET_UNKNOWN_HOST;
    for( cur = addr_list; cur != NULL; cur = cur->ai_next )
    {
        ctx->fd = (int) socket( cur->ai_family, cur->ai_socktype, 0 );
        if( ctx->fd < 0 )
        {
            ret = MBEDTLS_ERR_NET_SOCKET_FAILED;
            continue;
        }

        struct timeval con_tv;
        con_tv.tv_sec = 15;
        con_tv.tv_usec = 0;
        setsockopt(ctx->fd, SOL_SOCKET, SO_SNDTIMEO, &con_tv, sizeof(con_tv));
        setsockopt(ctx->fd, SOL_SOCKET, SO_RCVTIMEO, &con_tv, sizeof(con_tv));

        if( connect( ctx->fd, cur->ai_addr,cur->ai_addrlen ) == 0 )
        {
            ret = 0;
            break;
        }

        closesocket( ctx->fd );
        ret = MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    freeaddrinfo( addr_list );
    if(ret == 0){
        struct timeval tv;
        tv.tv_sec = HTTPS_READ_TIME_OUT_TICK / 1000;
        tv.tv_usec = (HTTPS_READ_TIME_OUT_TICK) * 1000;
        setsockopt(ctx->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(ctx->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    return( ret );
}

static int _httpSsl_conn(httpclient_t *client, char *host){
    int ret = -1;
    char port_str[6];

    HTTPC_LOG("SSL context init");
    httpclient_ssl_t *ssl = _sslContextInit();
    if(ssl == NULL){
        return HTTPCLIENT_ERROR_CONN;
    }
    client->ssl = ssl;
    HTTPC_LOG("SSL context init dont");
    if ((ret = mbedtls_ctr_drbg_seed(&(ssl->ctr_drbg), mbedtls_entropy_func, &(ssl->entropy), NULL,
                                     0)) != 0) {
        HTTPC_LOG("mbedtls_ctr_drbg_seed failed returned 0x%04x", ret < 0 ? -ret : ret);
        goto ERR_EXIT;
    }
    HTTPC_LOG("SSL mbedtls_ctr_drbg_seed dont");
    //
    if (client->verify_mode >= 1 && client->verify_mode <= 2 && NULL != client->server_cert)
    {
        if (0 != (ret = mbedtls_x509_crt_parse(&(ssl->cacert), (const unsigned char *)client->server_cert, client->server_cert_len)))
        {
            HTTPC_LOG(" failed ! x509parse_crt returned -0x%04x %d\n", -ret, strlen(client->server_cert));
            goto ERR_EXIT;
        }
    }
    //
    if(client->verify_mode == 2 &&  NULL != client->client_cert && NULL != client->client_pk){
        
        ret = mbedtls_x509_crt_parse(&(ssl->clicert), (const unsigned char *)client->client_cert, client->client_cert_len);
        if (ret != 0)
        {
            HTTPC_LOG(" failed!  mbedtls_x509_crt_parse returned -0x%x\n", -ret);
            goto ERR_EXIT;
        }

        ret = mbedtls_pk_parse_key(&(ssl->pkey), (const unsigned char *)client->client_pk, client->client_pk_len, \
            (const unsigned char *)client->pk_pwd, client->pk_len);
        if (ret != 0)
        {
            HTTPC_LOG(" failed\n  !  mbedtls_pk_parse_key returned -0x%x\n", -ret);
            goto ERR_EXIT;
        }
    }
    sprintf(port_str, "%u", client->remote_port);
    if (0 != (ret = _HttpNetConnect(&(ssl->net_ctx), host, port_str, MBEDTLS_NET_PROTO_TCP)))
    {
        HTTPC_LOG(" failed ! net_connect returned -0x%04x\n", -ret);
        goto ERR_EXIT;
    }

    // if ((ret = mbedtls_net_set_block(&(ssl->net_ctx))) != 0) {
    //     HTTPC_LOG("set block faliled returned 0x%04x", ret < 0 ? -ret : ret);
    //     goto ERR_EXIT;
    // }
    if ((ret = mbedtls_ssl_config_defaults(&(ssl->ssl_conf), MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
    {
        HTTPC_LOG(" failed! mbedtls_ssl_config_defaults returned %d\n", ret);
        goto ERR_EXIT;
    }
    mbedtls_ssl_conf_verify(&(ssl->ssl_conf), _serverCertificateVerify, (void *)host);
    mbedtls_ssl_conf_authmode(&(ssl->ssl_conf), client->verify_mode);
    if (client->verify_mode >= 1 && client->verify_mode <= 2 && NULL != client->server_cert){
        mbedtls_ssl_conf_ca_chain(&(ssl->ssl_conf), &(ssl->cacert), NULL);
    }
    if(client->verify_mode == 2 && NULL != client->client_cert && NULL != client->client_pk){
        if ((ret = mbedtls_ssl_conf_own_cert(&(ssl->ssl_conf), &(ssl->clicert), &(ssl->pkey))) != 0)
        {
            HTTPC_LOG(" failed\n  ! mbedtls_ssl_conf_own_cert returned %d\n", ret);
            goto ERR_EXIT;
        }
    }
    mbedtls_ssl_conf_rng(&(ssl->ssl_conf), _ssl_random, NULL);
    mbedtls_ssl_conf_dbg(&(ssl->ssl_conf), _ssl_debug, NULL);
    mbedtls_ssl_set_hostname(&(ssl->ssl_ctx), client->sni_host?client->sni_host:host);
    if ((ret = mbedtls_ssl_setup(&(ssl->ssl_ctx), &(ssl->ssl_conf))) != 0)
    {
        HTTPC_LOG("failed! mbedtls_ssl_setup returned %d\n", ret);
        goto ERR_EXIT;
    }

    mbedtls_ssl_set_bio(&(ssl->ssl_ctx), &(ssl->net_ctx), _ssl_net_send, _ssl_net_recv, NULL);
    // ret = mbedtls_net_connect(&(ssl->net_ctx), host, port_str, MBEDTLS_NET_PROTO_TCP);
    // if(ret != 0){
    //     HTTPC_LOG("failed! mbedtls_net_connect ret: -0x%x\n", -ret);
    //     goto ERR_EXIT;
    // }

    while ((ret = mbedtls_ssl_handshake(&(ssl->ssl_ctx))) != 0)
    {
        if ((ret != MBEDTLS_ERR_SSL_WANT_READ) && (ret != MBEDTLS_ERR_SSL_WANT_WRITE))
        {
            HTTPC_LOG("failed  ! mbedtls_ssl_handshake returned -0x%04x\n", -ret);
            goto ERR_EXIT;
        }
    }

    if ((ret = mbedtls_ssl_get_verify_result(&(ssl->ssl_ctx))) != 0) {
        HTTPC_LOG("mbedtls_ssl_get_verify_result failed returned 0x%04x", ret < 0 ? -ret : ret);
        goto ERR_EXIT;
    }
    HTTPC_LOG(" ok\n");
    return HTTPCLIENT_OK;
ERR_EXIT:
    //http_close 会free，这里就不用了
    // _sslContextfree(ssl);
    // client->ssl = NULL;
    return HTTPCLIENT_ERROR_CONN;
}


static int _httpSslRead(httpclient_ssl_t *ssl, char *buffer, int len, int timeout_ms)
{
    uint32_t readLen = 0;
    static int net_status = 0;
    int ret = -1;
	uint32_t retry = 10;

	if(timeout_ms > HTTPS_READ_TIME_OUT_TICK)
	{
		retry =  HTTPS_RETRY_READ_TIMES;
		timeout_ms = HTTPS_READ_TIME_OUT_TICK;
	}
	
    mbedtls_ssl_conf_read_timeout(&(ssl->ssl_conf), timeout_ms);
    while (readLen < len)
    {
        ret = mbedtls_ssl_read(&(ssl->ssl_ctx), (unsigned char *)(buffer + readLen), (len - readLen));
        HTTPC_LOG("ssl recv, code=0x%x, len=%d\n", -ret, readLen);
        if (ret > 0)
        {
            readLen += ret;
            net_status = 0;
        }
        else if (ret == 0)
        {
            /* if ret is 0 and net_status is -2, indicate the connection is closed during last call */
            return (net_status == -2) ? net_status : readLen;
        }
        else
        {
            if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                net_status = 0;
                break;
            }
            if (MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY == ret)
            {
                HTTPC_LOG("ssl recv error: code = -0x%x\n", -ret);
                net_status = -2; /* connection is closed */
                break;
            }
            else if ((MBEDTLS_ERR_SSL_TIMEOUT == ret) || (MBEDTLS_ERR_SSL_CONN_EOF == ret) || (MBEDTLS_ERR_SSL_SESSION_TICKET_EXPIRED == ret) || (MBEDTLS_ERR_SSL_NON_FATAL == ret))
            {
                /* read already complete */
                /* if call mbedtls_ssl_read again, it will return 0 (means EOF) */
				if(retry > 0)
				{
					retry--;
                    osiThreadSleep(10);
					continue;
				}
				else
				{
					return readLen;
				}
            }
            else
            {
                HTTPC_LOG("ssl recv error: code = %d\n", ret);
                net_status = -1;
                return -1; /* Connection error */
            }
        }
    }

    return (readLen > 0) ? readLen : net_status;
}

static int _httpSslWrite(httpclient_ssl_t *ssl, const char *buffer, int len, int timeout_ms)
{
    uint32_t writtenLen = 0;
    int ret = -1;

    if (ssl == NULL)
    {
        return -1;
    }

    while (writtenLen < len)
    {
        ret = mbedtls_ssl_write(&(ssl->ssl_ctx), (unsigned char *)(buffer + writtenLen), (len - writtenLen));
        if (ret > 0)
        {
            writtenLen += ret;
            continue;
        }
        else if (ret == 0)
        {
            HTTPC_LOG("ssl write timeout\n");
            return 0;
        }
        else
        {
            if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                break;
            }
            HTTPC_LOG("ssl write fail, code=%d\n", ret);
            return -1; /* Connnection error */
        }
    }
    return writtenLen;
}

int httpclient_parse_url(const char *url, char *scheme, size_t max_scheme_len, char *host, size_t maxhost_len, int *port, char *path, size_t max_path_len)
{
    char *scheme_ptr = (char *) url;
    char *host_ptr = (char *) strstr(url, "://");
    size_t host_len = 0;
    size_t path_len;
    char *port_ptr;
    char *path_ptr;
    char *fragment_ptr;

    if (host_ptr == NULL) {
        HTTPC_LOG("\nCould not find host\n");
        return HTTPCLIENT_ERROR_PARSE; /* URL is invalid */
    }

    if ( max_scheme_len < host_ptr - scheme_ptr + 1 ) { /* including NULL-terminating char */
        HTTPC_LOG("\nScheme str is too small (%d >= %d)\n", (int)(max_scheme_len), (int)(host_ptr - scheme_ptr + 1));
        return HTTPCLIENT_ERROR_PARSE;
    }
    memcpy(scheme, scheme_ptr, host_ptr - scheme_ptr);
    scheme[host_ptr - scheme_ptr] = '\0';

    host_ptr += 3;

    port_ptr = strchr(host_ptr, ':');
    if ( port_ptr != NULL ) {
        uint16_t tport;
        host_len = port_ptr - host_ptr;
        port_ptr++;
        if ( sscanf(port_ptr, "%hu", &tport) != 1) {
            HTTPC_LOG("\nCould not find port\n");
            return HTTPCLIENT_ERROR_PARSE;
        }
        *port = (int)tport;
    } else {
        *port = 0;
    }
    path_ptr = strchr(host_ptr, '/');
    if ( host_len == 0 ) {
        if(path_ptr)
            host_len = path_ptr - host_ptr;
        else
            host_len = strlen(host_ptr);
    }

    if ( maxhost_len < host_len + 1 ) { /* including NULL-terminating char */
        HTTPC_LOG("\nHost str is too small (%d >= %d)\n", (int)(maxhost_len), (int)(host_len + 1));
        return HTTPCLIENT_ERROR_PARSE;
    }
    memcpy(host, host_ptr, host_len);
    host[host_len] = '\0';

    fragment_ptr = strchr(host_ptr, '#');
    if (fragment_ptr != NULL) {
        path_len = fragment_ptr - path_ptr;
    } else {
        path_len = strlen(path_ptr);
    }

    if ( max_path_len < path_len + 1 ) { /* including NULL-terminating char */
        HTTPC_LOG("\nPath str is too small (%d >= %d)\n", (int)(max_path_len), (int)(path_len + 1));
        return HTTPCLIENT_ERROR_PARSE;
    }
    
    if(path_ptr){
        memcpy(path, path_ptr, path_len);
        path[path_len] = '\0';
    }else{
        strcpy(path, "/");
    }
    

    return HTTPCLIENT_OK;
}

int httpclient_get_info(httpclient_t *client, char *send_buf, int *send_idx, char *buf, size_t len)   /* 0 on success, err code on failure */
{
    int ret ;
    int cp_len ;
    int idx = *send_idx;

    if (len == 0) {
        len = strlen(buf);
    }

    do {
        if ((HTTPCLIENT_SEND_BUF_SIZE - idx) >= len) {
            cp_len = len ;
        } else {
            cp_len = HTTPCLIENT_SEND_BUF_SIZE - idx ;
        }

        memcpy(send_buf + idx, buf, cp_len) ;
        idx += cp_len ;
        len -= cp_len ;

        if (idx == HTTPCLIENT_SEND_BUF_SIZE) {
            if (client->is_http ) {
                ret = httpclient_tcp_send_all(client->socket, send_buf, HTTPCLIENT_SEND_BUF_SIZE) ;
            }else{
                ret = _httpSslWrite(client->ssl, send_buf, HTTPCLIENT_SEND_BUF_SIZE, 10000);
            }
            
            if (ret) {
                return (ret) ;
            }
        }
    } while (len) ;

    *send_idx = idx;
    return HTTPCLIENT_OK ;
}

void httpclient_set_custom_header(httpclient_t *client, char *header)
{
    client->header = header ;
}

int httpclient_basic_auth(httpclient_t *client, char *user, char *password)
{
    if ((strlen(user) + strlen(password)) >= HTTPCLIENT_AUTHB_SIZE) {
        return HTTPCLIENT_ERROR ;
    }
    client->auth_user = user;
    client->auth_password = password;
    return HTTPCLIENT_OK ;
}

int httpclient_send_auth(httpclient_t *client, char *send_buf, int *send_idx)
{
    char b_auth[(int)((HTTPCLIENT_AUTHB_SIZE + 3) * 4 / 3 + 1)] ;
    char base64buff[HTTPCLIENT_AUTHB_SIZE + 3] ;

    httpclient_get_info(client, send_buf, send_idx, "Authorization: Basic ", 0) ;
    sprintf(base64buff, "%s:%s", client->auth_user, client->auth_password) ;
    HTTPC_LOG("\nbAuth: %s\n", base64buff) ;
    httpclient_base64enc(b_auth, base64buff) ;
    b_auth[strlen(b_auth) + 1] = '\0' ;
    b_auth[strlen(b_auth)] = '\n' ;
    HTTPC_LOG("\nb_auth:%s\n", b_auth) ;
    httpclient_get_info(client, send_buf, send_idx, b_auth, 0) ;
    return HTTPCLIENT_OK ;
}


int httpclient_tcp_send_all(int sock_fd, char *data, int length)
{
    int written_len = 0;

    while (written_len < length) {
        int ret = send(sock_fd, data + written_len, length - written_len, 0);
        if (ret > 0) {
            written_len += ret;
            continue;
        } else if (ret == 0) {
            return written_len;
        } else {
            return -1; /* Connnection error */
        }
    }

    return written_len;
}

int httpclient_send_header(httpclient_t *client, char *url, int method, httpclient_data_t *client_data)
{
    char scheme[8] = {0};
    char host[HTTPCLIENT_MAX_HOST_LEN] = {0};
    char path[HTTPCLIENT_MAX_URL_LEN] = {0};
    int len;
    char send_buf[HTTPCLIENT_SEND_BUF_SIZE] = {0};
    char buf[HTTPCLIENT_SEND_BUF_SIZE] = {0};
    char *meth = (method == HTTPCLIENT_GET) ? "GET" : (method == HTTPCLIENT_POST) ? "POST" : (method == HTTPCLIENT_PUT) ? "PUT" : (method == HTTPCLIENT_DELETE) ? "DELETE" : (method == HTTPCLIENT_HEAD) ? "HEAD" : "";
    int ret, port;

    /* First we need to parse the url (http[s]://host[:port][/[path]]) */
    int res = httpclient_parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &(port), path, sizeof(path));
    if (res != HTTPCLIENT_OK) {
        HTTPC_LOG("\nhttpclient_parse_url returned %d\n", res);
        return res;
    }

    /* Send request */
    memset(send_buf, 0, HTTPCLIENT_SEND_BUF_SIZE);
    len = 0 ; /* Reset send buffer */

    snprintf(buf, sizeof(buf), "%s %s HTTP/1.1\r\nHost: %s\r\n", meth, path, client->sni_host?client->sni_host:host);
    ret = httpclient_get_info(client, send_buf, &len, buf, strlen(buf));
    if (ret) {
        HTTPC_LOG("\nCould not write request\n");
        return HTTPCLIENT_ERROR_CONN;
    }

    /* Send all headers */
    if (client->auth_user) {
        httpclient_send_auth(client, send_buf, &len) ; /* send out Basic Auth header */
    }

    /* Add user header information */
    if (client->header) {
        httpclient_get_info(client, send_buf, &len, (char *)client->header, strlen(client->header));
    }

    if ( client_data->post_buf != NULL ) {
        if(client_data->post_data_cb){
            snprintf(buf, sizeof(buf), "Content-Length: %d\r\n", client_data->seg_content_len);
        } else {
            snprintf(buf, sizeof(buf), "Content-Length: %d\r\n", client_data->post_buf_len);
        }
        httpclient_get_info(client, send_buf, &len, buf, strlen(buf));

        if (client_data->post_content_type != NULL)  {
            snprintf(buf, sizeof(buf), "Content-Type: %s\r\n", client_data->post_content_type);
            httpclient_get_info(client, send_buf, &len, buf, strlen(buf));
        }
    }

    /* Close headers */
    httpclient_get_info(client, send_buf, &len, "\r\n", 0);

    HTTPC_LOG("\nTrying to write %d bytes http header:%s\n", len, send_buf);
    if(client->is_http){
        ret = httpclient_tcp_send_all(client->socket, send_buf, len);
    }else{
        ret = _httpSslWrite(client->ssl, send_buf, len, 10000);
    }
    
    if (ret > 0) {
        HTTPC_LOG("\nWritten %d bytes, socket = %d\n", ret, client->socket);
    } else if ( ret == 0 ) {
        HTTPC_LOG("\nret == 0,Connection was closed by server\n");
        return HTTPCLIENT_CLOSED; /* Connection was closed by server */
    } else {
        HTTPC_LOG("\nConnection error (send returned %d)\n", ret);
        return HTTPCLIENT_ERROR_CONN;
    }

    return HTTPCLIENT_OK;
}

int httpclient_send_userdata(httpclient_t *client, httpclient_data_t *client_data)
{
    int ret = 0;

    if (client_data->post_buf && client_data->post_buf_len) {
        HTTPC_LOG("\nclient_data->post_buf:%s\n", client_data->post_buf);
        if(client_data->post_data_cb){
            int remain_size = client_data->seg_content_len;
            while (remain_size > 0)
            {
                int r_size = client_data->post_data_cb(client_data->seg_content_len-remain_size, client_data->post_buf, MIN(client_data->post_buf_len, remain_size));
                if(r_size <= 0 || r_size > MIN(client_data->post_buf_len, remain_size)){
                    return HTTPCLIENT_ERROR;
                }
                if(client->is_http){
                    ret = httpclient_tcp_send_all(client->socket, client_data->post_buf, r_size);
                }else{
                    ret = _httpSslWrite(client->ssl, client_data->post_buf, r_size, 10000);
                }
                if (ret > 0) {
                    remain_size -= ret;
                } else if ( ret == 0 ) {
                    HTTPC_LOG("\nret == 0,Connection was closed by server\n");
                    return HTTPCLIENT_CLOSED; /* Connection was closed by server */
                } else {
                    HTTPC_LOG("\nConnection error (send returned %d)\n", ret);
                    return HTTPCLIENT_ERROR_CONN;
                }
            }
        } else {
            if(client->is_http){
                ret = httpclient_tcp_send_all(client->socket, client_data->post_buf, client_data->post_buf_len);
            }else{
                ret = _httpSslWrite(client->ssl, client_data->post_buf, client_data->post_buf_len, 10000);
            }
            if (ret > 0) {
                HTTPC_LOG("\nWritten %d bytes\n", ret);
            } else if ( ret == 0 ) {
                HTTPC_LOG("\nret == 0,Connection was closed by server\n");
                return HTTPCLIENT_CLOSED; /* Connection was closed by server */
            } else {
                HTTPC_LOG("\nConnection error (send returned %d)\n", ret);
                return HTTPCLIENT_ERROR_CONN;
            }
        }
    }

    return HTTPCLIENT_OK;
}

int httpclient_recv(httpclient_t *client, char *buf, int min_len, int max_len, int *p_read_len)   /* 0 on success, err code on failure */
{
    int ret = 0;
    size_t readLen = 0;
    struct timeval timeout;

    while (readLen < max_len) {
        buf[readLen] = '\0';
        if (client->is_http) {
            // Set receive timeout
            timeout.tv_sec = 20;
            timeout.tv_usec = 0;
            ret = setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            if (ret < 0) {
                HTTPC_LOG("Set socket option failed");
            }
            ret = recv(client->socket, buf + readLen, max_len - readLen, 0);
        } else {
            ret = _httpSslRead(client->ssl, buf + readLen, max_len - readLen, 2000);
            HTTPC_LOG("\n_httpSslRead ret: %d\n", ret);
        }

        if (ret > 0) {
            readLen += ret;
            // If less than requested, peer closed connection or all data received
            if (ret < (max_len - readLen + ret)) {
                break;
            }
        } else if (ret == 0) {
            // Peer closed connection, all data received
            break;
        } else {
            if (readLen > 0)
                break;
            HTTPC_LOG("\nConnection error (recv returned %d errno: %d)\n", ret, errno);
            *p_read_len = readLen;
            return HTTPCLIENT_ERROR_CONN;
        }
    }

    HTTPC_LOG("\nRead %d bytes\n", (int)readLen);
    *p_read_len = readLen;
    buf[readLen] = '\0';

    return HTTPCLIENT_OK;
}

int httpclient_retrieve_content(httpclient_t *client, char *data, int len, httpclient_data_t *client_data)
{
    int count = 0;
    int templen = 0;
    int crlf_pos;
    /* Receive data */
    HTTPC_LOG("\nReceiving data:%s\n", data);
    client_data->is_more = true;

    if (client_data->response_content_len == -1 && client_data->is_chunked == false) {
        while(true)
        {
            int ret, max_len;
            if (count + len < client_data->response_buf_len - 1) {
                memcpy(client_data->response_buf + count, data, len);
                count += len;
                client_data->response_buf[count] = '\0';
            } else {
                memcpy(client_data->response_buf + count, data, client_data->response_buf_len - 1 - count);
                client_data->response_buf[client_data->response_buf_len - 1] = '\0';
                return HTTPCLIENT_RETRIEVE_MORE_DATA;
            }

            max_len = MIN(HTTPCLIENT_CHUNK_SIZE - 1, client_data->response_buf_len - 1 - count);
            ret = httpclient_recv(client, data, 1, max_len, &len);

            /* Receive data */
            HTTPC_LOG("\ndata len: %d %d\n", len, count);

            if (ret == HTTPCLIENT_ERROR_CONN) {
                HTTPC_LOG("\nret == HTTPCLIENT_ERROR_CONN\n");
                return ret;
            }

            if (len == 0) {/* read no more data */
                HTTPC_LOG("\nno more len == 0\n");
                client_data->is_more = false;
                return HTTPCLIENT_OK;
            }
        }
    }

    /* Added branch to support http download via Range method */
    // if(client_data->response_content_len > 0 && client_data->is_chunked == false)
    // {
    //     count = 0;
    //     while (true)
    //     {
    //         int ret = 0, to_read_len = 0, max_len = 0;

    //         max_len = MIN(HTTPCLIENT_CHUNK_SIZE - 1, client_data->response_buf_len - count);
    //         ret = httpclient_recv(client, data, 1, max_len, &len);

    //         if (ret == HTTPCLIENT_ERROR_CONN) {
    //             HTTPC_LOG("\nret == HTTPCLIENT_ERROR_CONN\n");
    //             return ret;
    //         }

    //         if (len == 0) {/* read no more data */
    //             HTTPC_LOG("\nno more len == 0\n");
    //             client_data->is_more = false;
    //             return HTTPCLIENT_OK;
    //         }

    //         memcpy(client_data->response_buf, data, len);
    //         client_data->response_buf_len = len;
    //         count += len;
    //         RTI_LOG("httpclient_recv: %d-%d-%d", len, count, client_data->response_buf_len);
    //     }
        
    // }

    while (true) {
        size_t readLen = 0;
        if ( client_data->is_chunked && client_data->retrieve_len <= 0) {
            /* Read chunk header */
            bool foundCrlf;
            int n;
            do {               
                HTTPC_LOG("\nlen: %d\n", len);
                foundCrlf = false;
                crlf_pos = 0;
                data[len] = 0;
                if (len >= 2) {
                    for (; crlf_pos < len - 2; crlf_pos++) {
                        if ( data[crlf_pos] == '\r' && data[crlf_pos + 1] == '\n' ) {
                            foundCrlf = true;
                            break;
                        }
                    }
                }
                if (!foundCrlf) { /* Try to read more */
                    if ( len < HTTPCLIENT_CHUNK_SIZE ) {
                        int new_trf_len, ret;
                        ret = httpclient_recv(client, data + len, 0, HTTPCLIENT_CHUNK_SIZE - len - 1, &new_trf_len);
                        len += new_trf_len;
                        if (ret == HTTPCLIENT_ERROR_CONN) {
                            return ret;
                        } else {
                            continue;
                        }
                    } else {
                        return HTTPCLIENT_ERROR;
                    }
                }
            } while (!foundCrlf);
            data[crlf_pos] = '\0';
            n = sscanf(data, "%x", (int *)&readLen);/* chunk length */
            client_data->retrieve_len = readLen;
            client_data->response_content_len += client_data->retrieve_len;
            if (n != 1) {
                HTTPC_LOG("\nCould not read chunk length\n");
                return HTTPCLIENT_ERROR_PRTCL;
            }

            memmove(data, &data[crlf_pos + 2], len - (crlf_pos + 2)); /* Not need to move NULL-terminating char any more */
            len -= (crlf_pos + 2);

            if ( readLen == 0 ) {
                /* Last chunk */
                client_data->is_more = false;
                HTTPC_LOG("\nno more (last chunk)\n");
                break;
            }
        } else {
            readLen = client_data->retrieve_len;
        }

        HTTPC_LOG("\nRetrieving %d bytes, len:%d\n", (int)readLen, len);

        do {            
            HTTPC_LOG("\nreadLen %d, len:%d\n", (int)readLen, len);
            templen = MIN(len, readLen);
            if (count + templen < client_data->response_buf_len - 1) {
                memcpy(client_data->response_buf + count, data, templen);
                count += templen;
                client_data->response_buf[count] = '\0';
                client_data->retrieve_len -= templen;
            } else {
                memcpy(client_data->response_buf + count, data, client_data->response_buf_len - 1 - count);
                client_data->response_buf[client_data->response_buf_len - 1] = '\0';
                client_data->retrieve_len -= (client_data->response_buf_len - 1 - count);
                return HTTPCLIENT_RETRIEVE_MORE_DATA;
            }

            // if ( len > readLen ) {            
            if ( len >= readLen ) {
                HTTPC_LOG("\nmemmove %d %d %d\n", (int)readLen, len, client_data->retrieve_len);
                memmove(data, &data[readLen], len - readLen); /* chunk case, read between two chunks */
                len -= readLen;
                readLen = 0;
                client_data->retrieve_len = 0;
            } else {
                readLen -= len;
            }
            
            if (readLen) {
                int ret;
                // int max_len = MIN(HTTPCLIENT_CHUNK_SIZE - 1, client_data->response_buf_len - 1 - count);                
                int max_len = MIN(MIN(HTTPCLIENT_CHUNK_SIZE - 1, client_data->response_buf_len - 1 - count), readLen);
                ret = httpclient_recv(client, data, 1, max_len, &len);
                if (ret == HTTPCLIENT_ERROR_CONN) {
                    return ret;
                }
            }
        } while (readLen);

        if ( client_data->is_chunked ) {
            if (len < 2) {
                int new_trf_len, ret;
                /* Read missing chars to find end of chunk */
                ret = httpclient_recv(client, data + len, 2 - len, HTTPCLIENT_CHUNK_SIZE - len - 1, &new_trf_len);
                if (ret == HTTPCLIENT_ERROR_CONN) {
                    return ret;
                }
                len += new_trf_len;
            }
            if ( (data[0] != '\r') || (data[1] != '\n') ) {
                HTTPC_LOG("\nFormat error, %s\n", data); /* after memmove, the beginning of next chunk */
                return HTTPCLIENT_ERROR_PRTCL;
            }
            memmove(data, &data[2], len - 2); /* remove the \r\n */
            len -= 2;
        } else {
            HTTPC_LOG("\nno more(content-length)\n");
            client_data->is_more = false;
            break;
        }

    }

    return HTTPCLIENT_OK;
}

int httpclient_response_parse(httpclient_t *client, char *data, int len, httpclient_data_t *client_data)
{
    int crlf_pos;
    int header_buf_len = client_data->header_buf_len;
    char *header_buf = client_data->header_buf;

    // reset the header buffer
    memset(header_buf, 0, header_buf_len);
    
    client_data->response_content_len = -1;

    char *crlf_ptr = strstr(data, "\r\n");
    if (crlf_ptr == NULL) {
        HTTPC_LOG("\r\n not found");
        return HTTPCLIENT_ERROR_PRTCL;
    }

    crlf_pos = crlf_ptr - data;
    data[crlf_pos] = '\0';

    /* Parse HTTP response */
    if ( sscanf(data, "HTTP/%*d.%*d %d %*[^\r\n]", &(client->response_code)) != 1 ) {
        /* Cannot match string, error */
        HTTPC_LOG("\nNot a correct HTTP answer : %s\n", data);
        return HTTPCLIENT_ERROR_PRTCL;
    }

    if ( (client->response_code < 200) || (client->response_code >= 400) ) {
        /* Did not return a 2xx code; TODO fetch headers/(&data?) anyway and implement a mean of writing/reading headers */
        HTTPC_LOG("\nResponse code %d\n", client->response_code);
    }

    HTTPC_LOG("\nReading headers%s\n", data);

    memmove(data, &data[crlf_pos + 2], len - (crlf_pos + 2) + 1); /* Be sure to move NULL-terminating char as well */
    len -= (crlf_pos + 2);

    client_data->is_chunked = false;

    /* Now get headers */
    while ( true ) {
        char *colon_ptr, *key_ptr, *value_ptr;
        int key_len, value_len;
        
        crlf_ptr = strstr(data, "\r\n");
        if (crlf_ptr == NULL) {
            if ( len < HTTPCLIENT_CHUNK_SIZE - 1 ) {
                int new_trf_len, ret;
                ret = httpclient_recv(client, data + len, 1, HTTPCLIENT_CHUNK_SIZE - len - 1, &new_trf_len);
                len += new_trf_len;
                data[len] = '\0';
                HTTPC_LOG("\nRead %d chars; In buf: [%s]\n", new_trf_len, data);
                if (ret == HTTPCLIENT_ERROR_CONN) {
                    return ret;
                } else {
                    continue;
                }
            } else {
                HTTPC_LOG("\nheader len > chunksize\n");
                return HTTPCLIENT_ERROR;
            }
        }

        crlf_pos = crlf_ptr - data;        
        if (crlf_pos == 0) { /* End of headers */
            memmove(data, &data[2], len - 2 + 1); /* Be sure to move NULL-terminating char as well */
            len -= 2;
            break;
        }
        
        colon_ptr = strstr(data, ": ");        
        if (colon_ptr) {             
            if (header_buf_len >= crlf_pos + 2) {
                /* copy response header to caller buffer */
                memcpy(header_buf, data, crlf_pos + 2);                                
                header_buf += crlf_pos + 2;
                header_buf_len -= crlf_pos + 2;
            }
            
            key_len = colon_ptr - data;
            value_len = crlf_ptr - colon_ptr - strlen(": ");            
            key_ptr = data;
            value_ptr = colon_ptr + strlen(": ");

            HTTPC_LOG("\nRead header : %.*s: %.*s\n", key_len, key_ptr, value_len, value_ptr); 
            if (0 == strncasecmp(key_ptr, "Content-Length", key_len)) {
                sscanf(value_ptr, "%d[^\r]", &(client_data->response_content_len));                
                client_data->retrieve_len = client_data->response_content_len;
            } else if (0 == strncasecmp(key_ptr, "Transfer-Encoding", key_len)) {
                if (0 == strncasecmp(value_ptr, "Chunked", value_len)) {
                    client_data->is_chunked = true;
                    client_data->response_content_len = 0;
                    client_data->retrieve_len = 0;
                }
            } 
           
            memmove(data, &data[crlf_pos + 2], len - (crlf_pos + 2) + 1); /* Be sure to move NULL-terminating char as well */
            len -= (crlf_pos + 2);
        } else {
            HTTPC_LOG("\nCould not parse header\n");
            return HTTPCLIENT_ERROR;
        }
    }
    if(client->method != HTTPCLIENT_HEAD)
        return httpclient_retrieve_content(client, data, len, client_data);
    else
        return HTTPCLIENT_OK;
}


HTTPCLIENT_RESULT httpclient_connect(httpclient_t *client, char *url)
{
    int ret = HTTPCLIENT_ERROR_CONN;
    char host[HTTPCLIENT_MAX_HOST_LEN] = {0};
    char scheme[8] = {0};
    char path[HTTPCLIENT_MAX_URL_LEN] = {0};
    
    /* First we need to parse the url (http[s]://host[:port][/[path]]) */
    int res = httpclient_parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &(client->remote_port), path, sizeof(path));
    if (res != HTTPCLIENT_OK) {
        HTTPC_LOG("\nhttpclient_parse_url returned %d\n", res);
        return (HTTPCLIENT_RESULT)res;
    }

    // http or https
    if (strcmp(scheme, "https") == 0) 
        client->is_http = false;
    else if (strcmp(scheme, "http") == 0)
        client->is_http = true;

    // default http 80 port, https 443 port
    if (client->remote_port == 0) {
        if (client->is_http) { 
            client->remote_port = HTTP_PORT;
        } else {
            client->remote_port = HTTPS_PORT;
        }
    }
        
    HTTPC_LOG("\nhttp?:%d, port:%d, host:%s\n", client->is_http, client->remote_port, host);

    client->socket = -1;
    if (client->is_http){
        ret = _httpTcp_conn(client, host);
    }else{
        ret = _httpSsl_conn(client, host);
    }

    HTTPC_LOG("\nhttpclient_connect() result:%d, client:%p\n", ret, client);
    return (HTTPCLIENT_RESULT)ret;
}

HTTPCLIENT_RESULT httpclient_send_request(httpclient_t *client, char *url, int method, httpclient_data_t *client_data)
{
    int ret = HTTPCLIENT_ERROR_CONN;

    if (client->is_http && client->socket < 0) {
        return (HTTPCLIENT_RESULT)ret;
    }

    if (client->is_http == false && client->ssl == NULL) {
        return (HTTPCLIENT_RESULT)ret;
    }

    ret = httpclient_send_header(client, url, method, client_data);
    if (ret != 0) {
        return (HTTPCLIENT_RESULT)ret;
    }

    if (method == HTTPCLIENT_POST || method == HTTPCLIENT_PUT) {
        ret = httpclient_send_userdata(client, client_data);
    }
   
    HTTPC_LOG("\nhttpclient_send_request() result:%d, client:%p\n", ret, client);
    return (HTTPCLIENT_RESULT)ret;
}

HTTPCLIENT_RESULT httpclient_recv_response(httpclient_t *client, httpclient_data_t *client_data)
{
    int reclen = 0;
    int ret = HTTPCLIENT_ERROR_CONN;
    // TODO: header format:  name + value must not bigger than HTTPCLIENT_CHUNK_SIZE.
    char buf[HTTPCLIENT_CHUNK_SIZE] = {0}; // char buf[HTTPCLIENT_CHUNK_SIZE*2] = {0};

    if (client->is_http && client->socket < 0) {
        return (HTTPCLIENT_RESULT)ret;
    }

    if (client->is_http == false && client->ssl == NULL) {
        return (HTTPCLIENT_RESULT)ret;
    }
    HTTPC_LOG("\nStart to receive response from server is_more:%d\n", client_data->is_more );
    if (client_data->is_more) {
        client_data->response_buf[0] = '\0';
        ret = httpclient_retrieve_content(client, buf, reclen, client_data);
    } else {
        int total_len = 0;
        bool headers_complete = false;

        /* Keep receiving until we have the full HTTP header (handles tiny first packets). */
        while (total_len < (HTTPCLIENT_CHUNK_SIZE - 1)) {
            ret = httpclient_recv(client,
                                  buf + total_len,
                                  1,
                                  (HTTPCLIENT_CHUNK_SIZE - 1) - total_len,
                                  &reclen);
            if (ret != 0) {
                return (HTTPCLIENT_RESULT)ret;
            }

            total_len += reclen;
            buf[total_len] = '\0';

            if (reclen == 0) {
                break;
            }

            if (strstr(buf, "\r\n\r\n")) {
                headers_complete = true;
                break;
            }
        }

        if (!headers_complete) {
            HTTPC_LOG("\nHTTP header incomplete before parsing\n");
            return HTTPCLIENT_ERROR_PRTCL;
        }

        HTTPC_LOG("\nreclen:%d, buf:%s\n", total_len, buf);
        ret = httpclient_response_parse(client, buf, total_len, client_data);
    }

    HTTPC_LOG("\nhttpclient_recv_response() result:%d, client:%p\n", ret, client);
    return (HTTPCLIENT_RESULT)ret;
}

void httpclient_close(httpclient_t *client)
{
    if (client->is_http) {
        if (client->socket >= 0)
            closesocket(client->socket);
    }else{
        closesocket(client->ssl->net_ctx.fd);
        _sslContextfree(client->ssl);
    }

    client->socket = -1;
    client->ssl = NULL;
    HTTPC_LOG("\nhttpclient_close() client:%p\n", client);
}

int httpclient_get_response_code(httpclient_t *client)
{
    return client->response_code;
}

static HTTPCLIENT_RESULT httpclient_common(httpclient_t *client, char *url, int method, httpclient_data_t *client_data)
{
    HTTPCLIENT_RESULT ret = HTTPCLIENT_ERROR_CONN;

    ret = httpclient_connect(client, url);

    if (!ret) {
        client->method = method;
        ret = httpclient_send_request(client, url, method, client_data);

        if (!ret) {
            ret = httpclient_recv_response(client, client_data);
        }
    }

    httpclient_close(client);

    return ret;
}

HTTPCLIENT_RESULT httpclient_get(httpclient_t *client, char *url, httpclient_data_t *client_data)
{
    return httpclient_common(client, url, HTTPCLIENT_GET, client_data);
}

HTTPCLIENT_RESULT httpclient_post(httpclient_t *client, char *url, httpclient_data_t *client_data)
{
    return httpclient_common(client, url, HTTPCLIENT_POST, client_data);
}

HTTPCLIENT_RESULT httpclient_put(httpclient_t *client, char *url, httpclient_data_t *client_data)
{
    return httpclient_common(client, url, HTTPCLIENT_PUT, client_data);
}

HTTPCLIENT_RESULT httpclient_delete(httpclient_t *client, char *url, httpclient_data_t *client_data)
{
    return httpclient_common(client, url, HTTPCLIENT_DELETE, client_data);
}

HTTPCLIENT_RESULT httpclient_head(httpclient_t *client, char *url, httpclient_data_t *client_data)
{
    return httpclient_common(client, url, HTTPCLIENT_HEAD, client_data);
}

int httpclient_get_response_header_value(char *header_buf, char *name, int *val_pos, int *val_len)
{    
    char *data = header_buf;
    char *crlf_ptr, *colon_ptr, *key_ptr, *value_ptr;
    int key_len, value_len;

    if (header_buf == NULL || name == NULL || val_pos == NULL  || val_len == NULL )
        return -1;
    
    while (true) {
        crlf_ptr = strstr(data, "\r\n");
        colon_ptr = strstr(data, ": ");        
        if (colon_ptr) {                         
            key_len = colon_ptr - data;
            value_len = crlf_ptr - colon_ptr - strlen(": ");            
            key_ptr = data;
            value_ptr = colon_ptr + strlen(": ");
            
            HTTPC_LOG("\nResponse header: %.*s: %.*s\n", key_len, key_ptr, value_len, value_ptr);
            if (0 == strncasecmp(key_ptr, name, key_len)) {
                *val_pos = value_ptr - header_buf;
                *val_len = value_len;
                return 0;
            } else {                 
                data = crlf_ptr + 2;
                continue;
            }
        } else 
            return -1;
            
    }
}

