
#include "lwip/sockets.h"
#include "ipv4/inet.h"
#include "lwip/netdb.h"
#include "osi_api.h"
#include <string.h>
#include "common_api.h"
#include "wificonfig.h"
#define AI_FLAG_WIFI_MAGIC 0x57494649 /* 'WIFI' marker to distinguish custom allocations */
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
#include "wifi.h"
#include "atiny_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include "network.h"
#endif

#if CONFIG_LOSCFG_COMPONENTS_NET_AT_DEBUG
#define SocketDebug SDK_LOG_I
#else
#define SocketDebug(FMT, ...)
#endif

extern const char *inet_ntop(int af, const void *src, char *dst, int size);
extern unsigned short int ntohs(unsigned short int n);

int id_convert_inter(int id)
{
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (id & 0xFFFF0000) {
        return (id & 0xBFFFFFFF) >> 16; //最多支持63个socket
    }

#endif
    return id;
}

int id_convert_ext(int id, uint8_t wifi_flag)
{
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (id == -1) {
        return -1;
    }

    if (wifi_flag) {
        return id;
    }

    return (id << 16) | 0x40000000; //最多支持63个socket
#else
    return id;
#endif
}

uint8_t is_wifi_socket_id(int inter_id)
{
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (inter_id & 0xFFFF0000) {
        return 0;
    }

    return 1;
#else
    return 0;
#endif
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    atiny_net_context *clientid = NULL;
    clientid = malloc(sizeof(atiny_net_context));

    if (clientid == NULL) {
        return -1;
    }

    memset(clientid, 0, sizeof(atiny_net_context));
    ret = atiny_net_accept(&getWifiSocketManager()->wifi_sockets[s].wifi_context, clientid, addr, 0, 0);

    if (ret != 0) {
        return -1;
    }

    if (clientid != NULL) {
        int socket_id = getFreeID();

        if (getWifiSocketManager()->wifi_sockets[socket_id].wifi_context != NULL) {
            free(getWifiSocketManager()->wifi_sockets[socket_id].wifi_context);
            getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = NULL;
        }

        getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = clientid;
        getWifiSocketManager()->wifi_sockets[socket_id].wifi_context->type = ATINY_SOCKET_TYPE_CLIENT;
        return id_convert_ext(socket_id, 1);
    }

    return -1;
cat1Return:
#endif
    ret = lwip_accept(s, addr, addrlen);
    errno = lwip_getsockerrno(s);
    return id_convert_ext(ret, 0);
}

int bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    getWifiSocketManager()->wifi_sockets[s].wifi_context->type = ATINY_SOCKET_TYPE_SERVER;
    getWifiSocketManager()->wifi_sockets[s].con_context.port = ((struct sockaddr_in *)name)->sin_port;
    getWifiSocketManager()->wifi_sockets[s].con_context.family = ((struct sockaddr_in *)name)->sin_family;

    char host[20];
    char port[10];
    int port_int = 0;
    memset(host, 0, sizeof(host));
    memset(port, 0, sizeof(port));
    port_int = ntohs(((struct sockaddr_in *)name)->sin_port);
    inet_ntop(((struct sockaddr_in *)name)->sin_family, (const void *)(&((struct sockaddr_in *)name)->sin_addr), host,
              sizeof(host));
    sprintf(port, "%d", port_int);

    if (((atiny_net_context *)atiny_net_bind(host, port, name->sa_family)) != 0) {
        return -1;
    }

    return 0;

cat1Return:
#endif

    ret = lwip_bind(s, name, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int shutdown(int s, int how)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_shutdown(s, how);
    errno = lwip_getsockerrno(s);
    return ret;
}

int closesocket(int s)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    // if (wifi_socket_is_enable() != 1) {
    //     return -1;
    // }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context->type == ATINY_SOCKET_TYPE_SERVER) {
        ret = atiny_net_server_close();
        // SocketDebug("close server socket");
    } else {
        ret = atiny_net_close(getWifiSocketManager()->wifi_sockets[s].wifi_context);
        // SocketDebug("close client socket");
    }

    SocketDebug("close socket[%d]", s);
    if(getWifiSocketManager()->wifi_sockets[s].wifi_context != NULL){
        free(getWifiSocketManager()->wifi_sockets[s].wifi_context);
        getWifiSocketManager()->wifi_sockets[s].wifi_context = NULL;
    }
    releaseID(s);
    return ret;
cat1Return:
#endif
    ret = lwip_close(s);
    errno = lwip_getsockerrno(s);
    return ret;
}

int connect(int s, const struct sockaddr *name, socklen_t namelen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    SocketDebug("ext id [%d]", s);

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    SocketDebug("inter id [%d]", s);

    if (wifi_socket_is_enable() != 1) {
        SocketDebug("control value not support wifi");
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    getWifiSocketManager()->wifi_sockets[s].con_context.port = ((struct sockaddr_in *)name)->sin_port;
    getWifiSocketManager()->wifi_sockets[s].con_context.family = ((struct sockaddr_in *)name)->sin_family;

    char host[20];
    char port[10];
    int port_int = 0;
    memset(host, 0, sizeof(host));
    memset(port, 0, sizeof(port));
    port_int = ntohs(((struct sockaddr_in *)name)->sin_port);
    inet_ntop(((struct sockaddr_in *)name)->sin_family, (const void *)(&((struct sockaddr_in *)name)->sin_addr), host,
              sizeof(host));
    sprintf(port, "%d", port_int);

    RTI_LOG("wifi connect to %s:%s", host, port);

    if (atiny_net_connect(getWifiSocketManager()->wifi_sockets[s].wifi_context, host, port,
                          getWifiSocketManager()->wifi_sockets[s].con_context.protocol) != 0) {
        //WIFI ����ʧ�ܣ��ͷ���Դ
        if(getWifiSocketManager()->wifi_sockets[s].wifi_context != NULL){
            free(getWifiSocketManager()->wifi_sockets[s].wifi_context);
            getWifiSocketManager()->wifi_sockets[s].wifi_context = NULL;
        }
        releaseID(s);
        return -1;
    }

    return 0;

cat1Return:
#endif
    ret = lwip_connect(s, name, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int getsockname(int s, struct sockaddr *name, socklen_t *namelen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_getsockname(s, name, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int getpeername(int s, struct sockaddr *name, socklen_t *namelen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_getsockname(s, name, namelen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    if (optname == SO_SNDTIMEO) {
        int time_ms = (*(struct timeval *)optval).tv_sec * 1000;
        time_ms += (*(struct timeval *)optval).tv_usec / 1000;
        int opt[2];
        opt[0] = s;
        opt[1] = time_ms;
        wifi_set_opt(3, opt, sizeof(opt));
    } else if (optname == SO_RCVTIMEO) {
        int time_ms = (*(struct timeval *)optval).tv_sec * 1000;
        time_ms += (*(struct timeval *)optval).tv_usec / 1000;
        int opt[2];
        opt[0] = s;
        opt[1] = time_ms;
        wifi_set_opt(2, opt, sizeof(opt));
    }

    return 0;

cat1Return:
#endif
    ret = lwip_setsockopt(s, level, optname, optval, optlen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_getsockopt(s, level, optname, optval, optlen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int listen(int s, int backlog)
{
    int ret = -1;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        return -1;
    }

    if (!isIDOccupied(s)) {
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        return -1;
    }

    return 0;
cat1Return:
#endif
    ret = lwip_listen(s, backlog);
    errno = lwip_getsockerrno(s);
    return ret;
}

int recv(int s, void *mem, size_t len, int flags)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        errno = ECONNRESET;
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        errno = ECONNRESET;
        return -1;
    }

    if (!isIDOccupied(s)) {
        errno = ECONNRESET;
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        errno = ECONNRESET;
        return -1;
    }
    RTI_LOG("+++ START wifi recv sock[%d] len[%d]", s, len);
    ret = atiny_net_recv(getWifiSocketManager()->wifi_sockets[s].wifi_context, mem, len);
    if (ret == -1) {
        errno = EAGAIN;
    }
    RTI_LOG("--- END wifi recv sock[%d] len[%d] ret[%d]", s, len, ret);

    return ret;

cat1Return:
#endif

    ret = lwip_recv(s, mem, len, flags);
    errno = lwip_getsockerrno(s);
    //RTI_LOG("RECV ERROR:%d", errno);
    return ret;
}

int recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_recvfrom(s, mem, len, flags, from, fromlen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int send(int s, const void *dataptr, size_t size, int flags)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    if (wifi_socket_is_enable() != 1) {
        errno = ECONNRESET;
        return -1;
    }

    if (s < 0 || s > SOCKET_NUM_MAX) {
        errno = ECONNRESET;
        return -1;
    }

    if (!isIDOccupied(s)) {
        errno = ECONNRESET;
        return -1;
    }

    if (getWifiSocketManager()->wifi_sockets[s].wifi_context == NULL) {
        errno = ECONNRESET;
        return -1;
    }
    RTI_LOG("+++ START wifi send sock[%d] len[%d]", s, size);
    ret = atiny_net_send(getWifiSocketManager()->wifi_sockets[s].wifi_context, dataptr, size);
    RTI_LOG("--- END wifi send sock[%d] len[%d] ret[%d]", s, size, ret);
    if (ret == -1) {
        errno = EAGAIN;
    }

    return ret;

cat1Return:
#endif
    ret = lwip_send(s, dataptr, size, flags);
    errno = lwip_getsockerrno(s);
    return ret;
}

int sendto(int s, const void *dataptr, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return -1;
cat1Return:
#endif
    ret = lwip_sendto(s, dataptr, size, flags, to, tolen);
    errno = lwip_getsockerrno(s);
    return ret;
}

int socket(int domain, int type, int protocol)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (wifi_socket_is_enable() != 1) {
        SocketDebug("create LTE Socket");
        goto cat1Return;
    }

    SocketDebug("create WIFI Socket");

    int socket_id = getFreeID();

    if (socket_id < 0 || socket_id > SOCKET_NUM_MAX) {
        goto cat1Return; // use LTE socket
    }

    getWifiSocketManager()->wifi_sockets[socket_id].con_context.type = type;
    getWifiSocketManager()->wifi_sockets[socket_id].con_context.protocol = protocol == 17 ? ATINY_PROTO_UDP : ATINY_PROTO_TCP;

    if (getWifiSocketManager()->wifi_sockets[socket_id].wifi_context != NULL) {
        SocketDebug("free id[%d][%x]", socket_id, getWifiSocketManager()->wifi_sockets[socket_id].wifi_context);
        free(getWifiSocketManager()->wifi_sockets[socket_id].wifi_context);
        getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = NULL;
    }

    getWifiSocketManager()->wifi_sockets[socket_id].wifi_context = (atiny_net_context *)malloc(sizeof(atiny_net_context));

    if (getWifiSocketManager()->wifi_sockets[socket_id].wifi_context == NULL) {
        releaseID(socket_id);
        return -1;
    }

    getWifiSocketManager()->wifi_sockets[socket_id].wifi_context->fd = -1;
    getWifiSocketManager()->wifi_sockets[socket_id].wifi_context->type = ATINY_SOCKET_TYPE_CLIENT;
    return id_convert_ext(socket_id, 1);

cat1Return:
#endif

    ret = lwip_socket(domain, type, protocol);
    errno = lwip_getsockerrno(0);
    return id_convert_ext(ret, 0);
}

int select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

#endif
    ret = lwip_select(maxfdp1, readset, writeset, exceptset, timeout);
    errno = lwip_getsockerrno(0);
    return ret;
}

int ioctlsocket(int s, long cmd, void *argp)
{
    int ret = 0;
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET

    if (is_wifi_socket_id(s) == 0) {
        s = id_convert_inter(s);
        goto cat1Return;
    }

    s = id_convert_inter(s);

    return 0;
cat1Return:
#endif
    ret = lwip_ioctl(s, cmd, argp);
    errno = lwip_getsockerrno(s);
    return ret;
}

/**
 * @brief Thread-safe DNS lookup wrapper
 * @param name Hostname to resolve
 * @param host_buf User-provided buffer for storing hostent data
 * @param len Buffer length (must be >= 2*sizeof(char*) + 4 bytes for WiFi mode)
 * @return Pointer to hostent structure on success, NULL on failure
 * 
 * @note WiFi mode memory layout in host_buf:
 *       [0 ... 2*sizeof(char*)): Address list array [ip_ptr, NULL]
 *       [2*sizeof(char*) ... +4): IPv4 address (4 bytes)
 * @note LwIP mode uses standard gethostbyname_r, host_buf size should be >= 256 bytes recommended
 */
struct hostent *gethostbyname_safe(const char *name, void *host_buf, int len)
{
    struct hostent *result_buf = NULL;
    struct hostent *result = NULL;
    int err = 0;

    // Validate input parameters
    if (name == NULL || host_buf == NULL || len <= 0) {
        errno = EINVAL;
        return NULL;
    }

    // Allocate hostent structure
    result_buf = (struct hostent *)malloc(sizeof(struct hostent));
    if (result_buf == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memset(result_buf, 0, sizeof(struct hostent));

#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    // WiFi socket mode - use AT command for DNS resolution
    if (wifi_socket_is_enable() == 1) {
        // Calculate minimum buffer size: 2 pointers + 4 bytes IPv4 address
        const int min_required = 2 * sizeof(char*) + 4;
        if (len < min_required) {
            free(result_buf);
            errno = EFAULT;
            return NULL;
        }

        // Clear buffer
        memset(host_buf, 0, len);
        
        // Setup memory layout
        char **addr_list = (char **)host_buf;
        char *ip_addr = (char *)host_buf + 2 * sizeof(char*);
        
        // Call AT WiFi DNS lookup
        int32_t ret = atiny_net_get_host_ip_by_name((uint8_t *)name, (uint32_t *)ip_addr);
        if (ret != 0) {
            free(result_buf);
            errno = HOST_NOT_FOUND;
            return NULL;
        }

        // Fill hostent structure
        result_buf->h_name = (char *)name;
        result_buf->h_addrtype = AF_INET;
        result_buf->h_length = 4;
        result_buf->h_aliases = NULL;
        result_buf->h_addr_list = addr_list;
        result_buf->h_addr_list[0] = ip_addr;
        result_buf->h_addr_list[1] = NULL;

        return result_buf;
    }
#endif

    // LwIP mode - use standard gethostbyname_r
    err = lwip_gethostbyname_r(name, result_buf, host_buf, len, &result, &err);
    if (err != 0 || result == NULL) {
        free(result_buf);
        errno = (err == 0) ? HOST_NOT_FOUND : err;
        return NULL;
    }

    return result_buf;
}

int getaddrinfo(const char *nodename, const char *servname,
                const struct addrinfo *hints, struct addrinfo **res) { 
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    struct addrinfo *ai;
    struct sockaddr_in *sin;
#endif
    
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (wifi_socket_is_enable() != 1) {
        goto cat1Return;
    }else{
    }
    int port = atoi(servname);
    // 分配 addrinfo 结构
    ai = (struct addrinfo *)malloc(sizeof(struct addrinfo));
    if (!ai) {
        return 203;
    }
    memset(ai, 0, sizeof(struct addrinfo));

    // 分配 sockaddr_in 结构
    sin = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    if (!sin) {
        free(ai);
        return 203;
    }
    memset(sin, 0, sizeof(struct sockaddr_in));

    // 填充 sockaddr_in 结构
    sin->sin_family = AF_INET;
    sin->sin_port = ((port & 0xff) << 8) | ((port & 0xff00) >> 8);
    int32_t ret = atiny_net_get_host_ip_by_name((uint8_t *)nodename, (uint32_t *)&sin->sin_addr);

    if (ret != 0) {
        //errno =
        free(ai);
        free(sin);
        return 201;
    }
    

    // 填充 addrinfo 结构
    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol =  hints->ai_protocol;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr = (struct sockaddr *)sin;
    ai->ai_flags = AI_FLAG_WIFI_MAGIC; /* mark as WiFi-allocated for safe free */
    ai->ai_next = NULL;

    *res = ai;
    return 0;
cat1Return:
#endif
    return lwip_getaddrinfo(nodename, servname, hints, res);
}

void freeaddrinfo(struct addrinfo *ai) {
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    /* WiFi path: only free buffers tagged with our magic */
    struct addrinfo *node = ai;
    bool all_wifi = true;
    while (node) {
        if (node->ai_flags != AI_FLAG_WIFI_MAGIC) {
            all_wifi = false;
            break;
        }
        node = node->ai_next;
    }

    if (all_wifi) {
        node = ai;
        while (node) {
            struct addrinfo *next = node->ai_next;
            if (node->ai_addr) {
                free(node->ai_addr);
            }
            free(node);
            node = next;
        }
        return;
    }else {
        goto cat1Return;
    }
cat1Return:
#endif
    lwip_freeaddrinfo(ai);
}

int getsockerrno(int s){
    return errno;
}