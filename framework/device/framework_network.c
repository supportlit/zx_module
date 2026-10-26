/**
 * @file framework_network.c
 * @brief Framework layer network management implementation using AT commands
 */

#include "framework_network.h"
#include "framework_msg.h"
#include "framework_log.h"
#include "framework_msg.h"
#include "framework_config.h"
#include "osi_api.h"
#include "at_api.h"
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
#include "wifi.h"
#endif
#include <string.h>
#include <stdio.h>
#include <time.h>
// #include <errno.h>
#include <sys/time.h>
#include "lwip/sockets.h"
#include "ipv4/inet.h"
#include "lwip/netdb.h"
#include "gpio_api.h"
#include "drv_def.h"
#include "drv_iomux.h"

#define TAG "NETWORK"

#define FRAMEWORK_OS_MS_PER_TICK 5U

#define FRAMEWORK_NTP_DEFAULT_INTERVAL_MS (6U * 60U * 60U * 1000U)
#define FRAMEWORK_NTP_MIN_INTERVAL_MS (60U * 1000U)
#define FRAMEWORK_NTP_DEFAULT_TIMEOUT_MS 5000U
#define FRAMEWORK_NTP_PORT_STR "123"
#define FRAMEWORK_NTP_TIMESTAMP_DELTA 2208988800ULL

/* Define bool type if not available */
#ifndef bool
typedef unsigned char bool;
#define true 1
#define false 0
#endif

typedef struct {
    uint8_t li_vn_mode;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;
    uint32_t orig_ts_frac;
    uint32_t recv_ts_sec;
    uint32_t recv_ts_frac;
    uint32_t trans_ts_sec;
    uint32_t trans_ts_frac;
} ntp_packet_t;

/* Network device information */
typedef struct {
    char imei[16];
    char iccid[24];
    char imsi[16];
    char apn[64];
    char cgpaddr[16];
    int lac;
    int csq;
    bool sim_ready;
    bool pdp_activated;
    char apweb_ssid[33];
} network_device_info_t;

typedef struct {
    osiTimer_t *timer;
    osiWork_t *work;
    char servers[FRAMEWORK_NTP_MAX_SERVERS][64];
    size_t server_count;
    size_t current_index;
    uint32_t interval_ms;
    uint32_t timeout_ms;
} network_ntp_ctx_t;

/* Network context */
typedef struct {
    network_state_e state;
    network_device_info_t dev_info;
    osiTimer_t *status_timer;
    osiWork_t *status_work;
    osiWork_t *sim_work;
    osiWork_t *bearer_work;
    bool initialized;
    bool start_network;
    framework_network_bearer_e bearer;
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    bool wifi_open;
    bool wifi_connected;
    struct {
        framework_wifi_config_mode_e mode;
        uint32_t timeout_ms;
        uint32_t start_tick;
    } wifi_config;
#endif
    network_ntp_ctx_t ntp;
    framework_network_bearer_e pending_bearer;
    bool bearer_switch_pending;
    bool bearer_switch_rearm;
} network_context_t;

static network_context_t g_network_ctx = {
    .state = NETWORK_STATE_DISCONNECTED,
    .dev_info = {{0}},
    .status_timer = NULL,
    .status_work = NULL,
    .sim_work = NULL,
    .bearer_work = NULL,
    .initialized = false,
    .start_network = false,
    .bearer = FRAMEWORK_NETWORK_BEARER_LTE,
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    .wifi_open = false,
    .wifi_connected = false,
    .wifi_config = {
        .mode = FRAMEWORK_WIFI_CONFIG_MODE_NONE,
        .timeout_ms = 0,
        .start_tick = 0,
    },
#endif
    .ntp = {0},
    .pending_bearer = FRAMEWORK_NETWORK_BEARER_LTE,
    .bearer_switch_pending = false,
    .bearer_switch_rearm = false,
};



static void prv_ntp_stop(void)
{
    if (g_network_ctx.ntp.timer) {
        osiTimerStop(g_network_ctx.ntp.timer);
    }
    if (g_network_ctx.ntp.work) {
        osiWorkCancel(g_network_ctx.ntp.work);
    }
}

static void prv_ntp_schedule(uint32_t delay_ms)
{
    if (g_network_ctx.ntp.timer == NULL || g_network_ctx.ntp.server_count == 0) {
        return;
    }
    osiTimerStop(g_network_ctx.ntp.timer);
    osiTimerStart(g_network_ctx.ntp.timer, delay_ms);
}

static framework_ntp_result_e prv_ntp_sync_one(const char *server, uint32_t timeout_ms, time_t *out_time)
{
    OSI_LOGI("Test-Time", "[prv_ntp_sync_one] Starting NTP sync with server='%s', timeout=%u ms",
             server ? server : "(null)", (unsigned int)timeout_ms);

    if (server == NULL || server[0] == '\0' || out_time == NULL) {
        OSI_LOGE("Test-Time", "[prv_ntp_sync_one] Invalid arguments (server is NULL or empty, or out_time is NULL)");
        return FRAMEWORK_NTP_ERR_DNS;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *res = NULL;
    OSI_LOGI("Test-Time", "[prv_ntp_sync_one] Calling getaddrinfo for server '%s', port '%s'...",
             server, FRAMEWORK_NTP_PORT_STR);
    int ret = getaddrinfo(server, FRAMEWORK_NTP_PORT_STR, &hints, &res);
    if (ret != 0 || res == NULL) {
        OSI_LOGE("Test-Time", "[prv_ntp_sync_one] DNS resolution failed for '%s', ret=%d, res=%p",
                 server, ret, res);
        return FRAMEWORK_NTP_ERR_DNS;
    }

    OSI_LOGI("Test-Time", "[prv_ntp_sync_one] DNS resolution succeeded for '%s'", server);

    framework_ntp_result_e last_err = FRAMEWORK_NTP_ERR_SOCKET;
    int addr_idx = 0;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next, addr_idx++) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        char *ip_str = inet_ntoa(ipv4->sin_addr);
        OSI_LOGI("Test-Time", "[prv_ntp_sync_one] Address #%d: IP=%s, port=%u",
                 addr_idx, ip_str ? ip_str : "unknown", (unsigned int)ntohs(ipv4->sin_port));

        int sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) {
            OSI_LOGE("Test-Time", "[prv_ntp_sync_one] Failed to create UDP socket: fd=%d", sock);
            last_err = FRAMEWORK_NTP_ERR_SOCKET;
            continue;
        }

        struct timeval tv = {
            .tv_sec = (time_t)(timeout_ms / 1000U),
            .tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U),
        };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, p->ai_addr, p->ai_addrlen) < 0) {
            OSI_LOGE("Test-Time", "[prv_ntp_sync_one] UDP connect failed to %s", ip_str ? ip_str : "addr");
            last_err = FRAMEWORK_NTP_ERR_SOCKET;
            closesocket(sock);
            continue;
        }

        ntp_packet_t packet;
        memset(&packet, 0, sizeof(packet));
        packet.li_vn_mode = 0x1b; /* LI=0, VN=3, Mode=3 (client) */

        OSI_LOGI("Test-Time", "[prv_ntp_sync_one] Sending 48-byte NTP request packet to %s...", ip_str ? ip_str : "addr");
        if (send(sock, &packet, sizeof(packet), 0) < 0) {
            OSI_LOGE("Test-Time", "[prv_ntp_sync_one] Failed to send NTP request to %s", ip_str ? ip_str : "addr");
            last_err = FRAMEWORK_NTP_ERR_SEND;
            closesocket(sock);
            continue;
        }

        OSI_LOGI("Test-Time", "[prv_ntp_sync_one] Waiting for NTP reply from %s (timeout=%u ms)...",
                 ip_str ? ip_str : "addr", (unsigned int)timeout_ms);
        int recv_bytes = recv(sock, &packet, sizeof(packet), 0);
        if (recv_bytes < 0) {
            OSI_LOGE("Test-Time", "[prv_ntp_sync_one] recv failed or timed out from %s, ret=%d",
                     ip_str ? ip_str : "addr", recv_bytes);
            last_err = FRAMEWORK_NTP_ERR_RECV;
            closesocket(sock);
            continue;
        }

        closesocket(sock);

        uint32_t secs = ntohl(packet.trans_ts_sec);
        uint32_t frac = ntohl(packet.trans_ts_frac);
        OSI_LOGI("Test-Time", "[prv_ntp_sync_one] Received %d bytes from %s: trans_ts_sec=%lu (0x%lx), trans_ts_frac=%lu",
                 recv_bytes, ip_str ? ip_str : "addr", (unsigned long)secs, (unsigned long)secs, (unsigned long)frac);

        if (secs == 0 || secs <= FRAMEWORK_NTP_TIMESTAMP_DELTA) {
            OSI_LOGE("Test-Time", "[prv_ntp_sync_one] Bad timestamp in reply: %lu <= delta %llu",
                     (unsigned long)secs, (unsigned long long)FRAMEWORK_NTP_TIMESTAMP_DELTA);
            last_err = FRAMEWORK_NTP_ERR_BAD_RESPONSE;
            continue;
        }

        *out_time = (time_t)(secs - FRAMEWORK_NTP_TIMESTAMP_DELTA);
        OSI_LOGI("Test-Time", "[prv_ntp_sync_one] SUCCESS! Parsed UNIX epoch timestamp: %ld (0x%lx)",
                 (long)(*out_time), (unsigned long)(*out_time));
        freeaddrinfo(res);
        return FRAMEWORK_NTP_RESULT_OK;
    }

    freeaddrinfo(res);
    OSI_LOGE("Test-Time", "[prv_ntp_sync_one] All addresses for server '%s' failed, last_err=%d", server, last_err);
    return last_err;
}

#define IST_TIMEZONE_OFFSET_SEC (5 * 3600 + 30 * 60) /* +5:30 = 19800 seconds */

static void prv_epoch_to_ositime(uint32_t epoch, osiTime_t *out)
{
    if (!out) return;
    uint32_t days = epoch / 86400;
    uint32_t rem = epoch % 86400;

    out->tm_hour = (int)(rem / 3600);
    out->tm_min  = (int)((rem % 3600) / 60);
    out->tm_sec  = (int)(rem % 60);

    /* Civil date calculation from days since 1970-01-01 (Euclidean affine algorithm) */
    uint32_t a = days + 719468;
    uint32_t b = (4 * a + 3) / 146097;
    uint32_t c = a - (146097 * b) / 4;
    uint32_t d_calc = (4 * c + 3) / 1461;
    uint32_t e = c - (1461 * d_calc) / 4;
    uint32_t m_calc = (5 * e + 2) / 153;

    out->tm_mday = (int)(e - (153 * m_calc + 2) / 5 + 1);
    out->tm_mon  = (int)(m_calc + 3 - 12 * (m_calc / 10));
    out->tm_year = (int)(b * 100 + d_calc + (m_calc / 10));
    out->tm_wday = (int)((days + 4) % 7);
}

static void prv_ntp_apply_time(time_t ts)
{
    osiTime_t before_sys = {0}, before_rtc = {0};
    osiGetSysTime(&before_sys);
    osiGetTime(&before_rtc);
    time_t before_time = time(NULL);

    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] ========== APPLYING NTP TIME ==========");
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] Input UTC NTP timestamp ts=%ld (0x%lx)", (long)ts, (unsigned long)ts);
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] BEFORE SET: time(NULL)=%ld", (long)before_time);
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] BEFORE SET: osiGetTimeZoneMin()=%ld", (long)osiGetTimeZoneMin());
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] BEFORE SET: SysTime=%04d-%02d-%02d %02d:%02d:%02d",
             before_sys.tm_year, before_sys.tm_mon, before_sys.tm_mday,
             before_sys.tm_hour, before_sys.tm_min, before_sys.tm_sec);
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] BEFORE SET: RtcTime=%04d-%02d-%02d %02d:%02d:%02d",
             before_rtc.tm_year, before_rtc.tm_mon, before_rtc.tm_mday,
             before_rtc.tm_hour, before_rtc.tm_min, before_rtc.tm_sec);

    /* Set Indian Standard Time timezone in RTOS: +5:30 = 330 minutes */
    if (osiGetTimeZoneMin() != 330) {
        OSI_LOGI("Test-Time", "[prv_ntp_apply_time] Setting RTOS timezone to IST (330 min)");
        osiSetTimeZoneMin(5 * 60 + 30);
    }

    /* Convert UTC NTP timestamp to IST (+5:30 = 19800 seconds) */
    uint32_t ist_ts = (uint32_t)ts + IST_TIMEZONE_OFFSET_SEC;
    osiTime_t rtc_time = {0};
    prv_epoch_to_ositime(ist_ts, &rtc_time);

    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] Calculated IST datetime: %04d-%02d-%02d %02d:%02d:%02d",
             rtc_time.tm_year, rtc_time.tm_mon, rtc_time.tm_mday,
             rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);

    /* 1. Set Hardware RTC directly to IST (osiSetTime) */
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] Setting IST to Hardware RtcTime: %04d-%02d-%02d %02d:%02d:%02d",
             rtc_time.tm_year, rtc_time.tm_mon, rtc_time.tm_mday,
             rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);
    osiSetTime(&rtc_time);

    /* 2. Set System time (osiSetSysTime).
     * osiSetSysTime internally subtracts 8 hours (kernel UTC+8 conversion)
     * from its input and modifies the struct in place. To make SysTime equal to IST,
     * we feed (IST + 8 hours) so that after the internal -8h conversion, SysTime == IST. */
    uint32_t sys_feed_ts = ist_ts + (8 * 3600);
    osiTime_t sys_time = {0};
    prv_epoch_to_ositime(sys_feed_ts, &sys_time);

    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] Setting IST to SysTime (feed +8h: %04d-%02d-%02d %02d:%02d:%02d)",
             sys_time.tm_year, sys_time.tm_mon, sys_time.tm_mday,
             sys_time.tm_hour, sys_time.tm_min, sys_time.tm_sec);
    osiSetSysTime(&sys_time);

    osiTime_t after_sys = {0}, after_rtc = {0};
    osiGetSysTime(&after_sys);
    osiGetTime(&after_rtc);
    time_t after_time = time(NULL);

    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] AFTER SET: time(NULL)=%ld", (long)after_time);
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] AFTER SET: SysTime=%04d-%02d-%02d %02d:%02d:%02d",
             after_sys.tm_year, after_sys.tm_mon, after_sys.tm_mday,
             after_sys.tm_hour, after_sys.tm_min, after_sys.tm_sec);
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] AFTER SET: RtcTime=%04d-%02d-%02d %02d:%02d:%02d",
             after_rtc.tm_year, after_rtc.tm_mon, after_rtc.tm_mday,
             after_rtc.tm_hour, after_rtc.tm_min, after_rtc.tm_sec);
    OSI_LOGI("Test-Time", "[prv_ntp_apply_time] ==========================================");
}

static void prv_ntp_work_handler(void *param)
{
    (void)param;
    OSI_LOGI("Test-Time", "[prv_ntp_work_handler] Worker triggered: server_count=%u, net_connected=%d",
             (unsigned int)g_network_ctx.ntp.server_count, framework_network_is_connected());

    if (g_network_ctx.ntp.server_count == 0) {
        OSI_LOGW("Test-Time", "[prv_ntp_work_handler] No NTP servers configured");
        return;
    }

    if (!framework_network_is_connected()) {
        OSI_LOGW("Test-Time", "[prv_ntp_work_handler] Network not connected! Sending MSG_NET_NTP_FAILURE");
        framework_msg_t msg = {
            .id = MSG_NET_NTP_FAILURE,
            .param1 = 0,
            .param2 = FRAMEWORK_NTP_ERR_NOT_CONNECTED,
            .param3 = 0,
        };
        framework_msg_send(&msg);
        prv_ntp_schedule(g_network_ctx.ntp.interval_ms);
        return;
    }

    size_t attempts = 0;
    size_t idx = g_network_ctx.ntp.current_index % g_network_ctx.ntp.server_count;
    framework_ntp_result_e last_err = FRAMEWORK_NTP_ERR_BAD_RESPONSE;
    time_t ts = 0;

    while (attempts < g_network_ctx.ntp.server_count) {
        const char *server = g_network_ctx.ntp.servers[idx];
        OSI_LOGI("Test-Time", "[prv_ntp_work_handler] Attempt %u/%u: Trying server[%u]='%s'",
                 (unsigned int)(attempts + 1), (unsigned int)g_network_ctx.ntp.server_count,
                 (unsigned int)idx, server ? server : "(null)");

        framework_ntp_result_e ret = prv_ntp_sync_one(server, g_network_ctx.ntp.timeout_ms, &ts);
        if (ret == FRAMEWORK_NTP_RESULT_OK) {
            OSI_LOGI("Test-Time", "[prv_ntp_work_handler] Sync OK from server '%s'! epoch ts=%ld",
                     server, (long)ts);
            prv_ntp_apply_time(ts);
            g_network_ctx.ntp.current_index = idx;

            framework_msg_t msg = {
                .id = MSG_NET_NTP_SUCCESS,
                .param1 = (uint32_t)idx,
                .param2 = (uint32_t)ts,
                .param3 = 0,
            };
            OSI_LOGI("Test-Time", "[prv_ntp_work_handler] Sending MSG_NET_NTP_SUCCESS (idx=%u, ts=%ld)",
                     (unsigned int)idx, (long)ts);
            framework_msg_send(&msg);

            prv_ntp_schedule(g_network_ctx.ntp.interval_ms);
            return;
        }

        OSI_LOGW("Test-Time", "[prv_ntp_work_handler] Server '%s' sync failed, ret=%d", server, ret);
        last_err = ret;
        idx = (idx + 1) % g_network_ctx.ntp.server_count;
        attempts++;
    }

    size_t failed_idx = (idx + g_network_ctx.ntp.server_count - 1) % g_network_ctx.ntp.server_count;
    OSI_LOGE("Test-Time", "[prv_ntp_work_handler] ALL %u servers failed! Sending MSG_NET_NTP_FAILURE (last_err=%d)",
             (unsigned int)g_network_ctx.ntp.server_count, last_err);
    framework_msg_t msg = {
        .id = MSG_NET_NTP_FAILURE,
        .param1 = (uint32_t)failed_idx,
        .param2 = (uint32_t)last_err,
        .param3 = 0,
    };
    framework_msg_send(&msg);

    g_network_ctx.ntp.current_index = idx;
    prv_ntp_schedule(g_network_ctx.ntp.interval_ms);
}

static void prv_ntp_timer_cb(void *param)
{
    (void)param;
    if (g_network_ctx.ntp.work) {
        osiWorkEnqueue(g_network_ctx.ntp.work, osiSysWorkQueueLowPriority());
    }
}

static void prv_bearer_switch_work(void *param)
{
    (void)param;
    framework_network_bearer_e target = g_network_ctx.pending_bearer;
    OSI_LOGI(TAG, "Bearer switch work start -> %s", target == FRAMEWORK_NETWORK_BEARER_WIFI ? "WiFi" : "LTE");
    framework_network_switch_bearer_sync(target);
    if (g_network_ctx.bearer_switch_rearm) {
        g_network_ctx.bearer_switch_rearm = false;
        osiWorkEnqueue(g_network_ctx.bearer_work, osiSysWorkQueueLowPriority());
        return;
    }
    g_network_ctx.bearer_switch_pending = false;
}

OSI_UNUSED static void prv_ntp_on_connected(void)
{
    prv_ntp_schedule(1000);
}

static void prv_ntp_on_disconnected(void)
{
    prv_ntp_stop();
}


#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
static void prv_schedule_status_timer(uint32_t interval_ms)
{
    if (g_network_ctx.status_timer == NULL) {
        return;
    }
    osiTimerStop(g_network_ctx.status_timer);
    osiTimerStart(g_network_ctx.status_timer, interval_ms);
}
#endif

#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
OSI_UNUSED static int prv_wifi_rssi_to_csq(int rssi)
{
    if (rssi <= -133)
        return 0;
    if (rssi == -111)
        return 1;
    if (rssi >= -109 && rssi <= -53)
        return (rssi + 113) / 2;
    if (rssi >= -52)
        return 31;
    return 99;
}

static void prv_notify_wifi_config_event(uint32_t msg_id,
                                         framework_wifi_config_mode_e mode,
                                         int status)
{
    framework_msg_t msg = {
        .id = msg_id,
        .param1 = (uint32_t)mode,
        .param2 = (uint32_t)status,
        .param3 = 0,
    };
    framework_msg_send(&msg);
}

static uint32_t prv_elapsed_ms(uint32_t start_tick)
{
    return (osiGetTicks() - start_tick) * FRAMEWORK_OS_MS_PER_TICK;
}

static void prv_handle_wifi_status(int *rssi_count)
{
    if(g_network_ctx.wifi_config.mode != FRAMEWORK_WIFI_CONFIG_MODE_NONE){
        *rssi_count = 0;
        g_network_ctx.state = NETWORK_STATE_CONNECTING;
        g_network_ctx.wifi_connected = false;
        return;
    }

    bool hw_open = (wifi_is_open() == 1);
    if (!hw_open) {
        g_network_ctx.wifi_open = false;
        if (g_network_ctx.wifi_connected) {
            g_network_ctx.wifi_connected = false;
            g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
            framework_msg_t msg = {.id = MSG_NET_DISCONNECTED};
            framework_msg_send(&msg);
        }
        return;
    }

    g_network_ctx.wifi_open = true;
    bool connected = (wifi_is_connected() == 1);
    if (connected) {
        int count = *rssi_count;
        if(count > 20 || count == 0){
            *rssi_count = 1;
            int rssi = 0;
            if (wifi_get_rssi(&rssi) == 0) {
                g_network_ctx.dev_info.csq = prv_wifi_rssi_to_csq(rssi);
            }
        }else{
            *rssi_count = count + 1;
        }
    }

    if (connected && !g_network_ctx.wifi_connected) {
        g_network_ctx.state = NETWORK_STATE_CONNECTED;
        framework_msg_t msg = {.id = MSG_NET_CONNECTED};
        framework_msg_send(&msg);
        prv_schedule_status_timer(10000);
    } else if (!connected && g_network_ctx.wifi_connected) {
        g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
        framework_msg_t msg = {.id = MSG_NET_DISCONNECTED};
        framework_msg_send(&msg);
        prv_ntp_on_disconnected();
    }

    g_network_ctx.wifi_connected = connected;
}

static void prv_poll_wifi_config(void)
{
    if (g_network_ctx.wifi_config.mode == FRAMEWORK_WIFI_CONFIG_MODE_NONE) {
        return;
    }

    framework_wifi_config_result_t result;
    memset(&result, 0, sizeof(result));
    int status = framework_network_wifi_config_poll(&result);
    if (status == 0) {
        return;
    }

    uint32_t msg_id = MSG_NET_WIFI_CONFIG_ERROR;
    if (status == 1 && result.success) {
        msg_id = MSG_NET_WIFI_CONFIG_SUCCESS;
    } else if (status == -2) {
        msg_id = MSG_NET_WIFI_CONFIG_TIMEOUT;
    }

    prv_notify_wifi_config_event(msg_id, result.mode, status);
    prv_schedule_status_timer(10000);
}

static void prv_prepare_softap_info(const framework_wifi_config_request_t *request, ap_mode_info_t *ap_info)
{
    char tmp_ssid[33] = {0};
    const char *ssid = NULL;

    if (request && request->softap_ssid && request->softap_ssid[0] != '\0') {
        ssid = request->softap_ssid;
    } else {
        if(strlen(g_network_ctx.dev_info.apweb_ssid) > 0 && g_network_ctx.dev_info.apweb_ssid){
            snprintf(tmp_ssid, sizeof(tmp_ssid), "%s", g_network_ctx.dev_info.apweb_ssid);
        }else{
            if (g_network_ctx.dev_info.imei[0] != '\0') {
                snprintf(tmp_ssid, sizeof(tmp_ssid), "SoundBox-%s", g_network_ctx.dev_info.imei);
            } else {
                snprintf(tmp_ssid, sizeof(tmp_ssid), "SoundBox-Setup");
            }
        }

        ssid = tmp_ssid;
    }

    size_t ssid_len = strlen(ssid);
    if (ssid_len > 32)
        ssid_len = 32;
    memcpy(ap_info->ssid.val, ssid, ssid_len);
    ap_info->ssid.val[ssid_len] = '\0';
    ap_info->ssid.len = (unsigned char)ssid_len;

    if (request && request->softap_password && request->softap_password[0] != '\0') {
        size_t pwd_len = strlen(request->softap_password);
        if (pwd_len > 64)
            pwd_len = 64;
        memcpy(ap_info->password, request->softap_password, pwd_len);
        ap_info->password[pwd_len] = '\0';
        ap_info->password_len = (int)pwd_len;
        ap_info->security_type = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_info->security_type = WIFI_AUTH_OPEN;
        ap_info->password_len = 0;
    }

    ap_info->channel = (request && request->softap_channel > 0) ? request->softap_channel : 0;
    ap_info->language = (request && request->language == FRAMEWORK_WIFI_CONFIG_LANG_CHINESE) ?
                        LANG_CHINESE : LANG_ENGLISH;
}
#endif

/**
 * @brief Update CSQ (signal strength)
 * @details Sends AT+CSQ command and parses response
 * @return true on success, false on failure
 */
static bool prv_update_csq(void)
{
    char resp[128] = {0};
    int csq = 99, tmp = 0;
    char *cmd = "AT+CSQ\r\n";
    
    int ret = atCmdSendWaitResp(cmd, 1, "+CSQ", 1, "ERROR", resp, sizeof(resp));
    if (ret == 0) {
        if (sscanf(resp, "+CSQ: %d,%d", &csq, &tmp) == 2) {
            g_network_ctx.dev_info.csq = csq;
            FRAMEWORK_LOG("CSQ updated: %d", csq);
            return true;
        }
    }
    return false;
}

/**
 * @brief Update IMEI (device identifier)
 * @details Sends AT+GSN command to retrieve IMEI
 * @return true on success, false on failure
 */
static bool prv_update_imei(void)
{
    char resp[128] = {0};
    char imei[16] = {0};
    char *cmd = "AT+GSN\r\n";
    
    int ret = atCmdSendWaitResp(cmd, 1, NULL, 1, "ERROR", resp, sizeof(resp));
    if (ret == 0) {
        if (sscanf(resp, "%15s", imei) == 1) {
            strncpy(g_network_ctx.dev_info.imei, imei, sizeof(g_network_ctx.dev_info.imei) - 1);
            FRAMEWORK_LOG("IMEI: %s", g_network_ctx.dev_info.imei);
            return true;
        }
    }
    return false;
}

/**
 * @brief Update ICCID (SIM card identifier)
 * @details Sends AT*ICCID? command to retrieve ICCID
 * @return true on success, false on failure
 */
static bool prv_update_iccid(void)
{
    char resp[128] = {0};
    char iccid[24] = {0};
    char *cmd = "AT*ICCID?\r\n";
    
    int ret = atCmdSendWaitResp(cmd, 1, "*ICCID", 1, "ERROR", resp, sizeof(resp));
    if (ret == 0) {
        if (sscanf(resp, "*ICCID: %23s", iccid) == 1) {
            strncpy(g_network_ctx.dev_info.iccid, iccid, sizeof(g_network_ctx.dev_info.iccid) - 1);
            FRAMEWORK_LOG("ICCID: %s", g_network_ctx.dev_info.iccid);
            return true;
        }
    }
    return false;
}

/**
 * @brief Update IMSI (subscriber identifier)
 * @details Sends AT+CIMI command to retrieve IMSI
 * @return true on success, false on failure
 */
static bool prv_update_imsi(void)
{
    char resp[128] = {0};
    char imsi[16] = {0};
    char *cmd = "AT+CIMI\r\n";
    
    int ret = atCmdSendWaitResp(cmd, 1, NULL, 1, "ERROR", resp, sizeof(resp));
    if (ret == 0) {
        /* Extract numeric part */
        int idx = 0;
        for (int i = 0; i < strlen(resp) && idx < 15; i++) {
            if (resp[i] >= '0' && resp[i] <= '9') {
                imsi[idx++] = resp[i];
            }
        }
        imsi[idx] = '\0';
        
        if (idx > 0) {
            strncpy(g_network_ctx.dev_info.imsi, imsi, sizeof(g_network_ctx.dev_info.imsi) - 1);
            FRAMEWORK_LOG("IMSI: %s", g_network_ctx.dev_info.imsi);
            return true;
        }
    }
    return false;
}

/**
 * @brief Check network registration status
 * @details Sends AT+CEREG? to check if device is registered to cellular network
 * @return 1 if registered (local or roaming), 0 if not registered
 */
static int prv_check_network_status(void)
{
    char resp[128] = {0};
    int n = 0, stat = 0;
    char *cmd = "AT+CEREG?\r\n";
    
    int ret = atCmdSendWaitResp(cmd, 1, "+CEREG", 1, "ERROR", resp, sizeof(resp));
    if (ret == 0) {
        if (sscanf(resp, "+CEREG: %d,%d", &n, &stat) >= 2) {
            /* stat: 1=registered home network, 5=registered roaming network */
            if (stat == 1 || stat == 5) {
                g_network_ctx.dev_info.lac = stat;
                return 1;  /* Registered */
            }
        }
    }
    return 0;  /* Not registered */
}

static int prv_network_LTE_set_enabled(bool enable)
{
    char cmd[32];
    int wifi_sock_en = enable ? 0 : 1;
    if (enable) {
        snprintf(cmd, sizeof(cmd), "AT+CFUN=1\r\n");
        wifi_sock_en = 0;
        wifi_set_opt(1, &wifi_sock_en, sizeof(wifi_sock_en));
    } else {
        snprintf(cmd, sizeof(cmd), "AT+CFUN=0\r\n");
        wifi_set_opt(1, &wifi_sock_en, sizeof(wifi_sock_en));
    }
    int ret = atCmdSendWaitResp(cmd, 1, "OK", 1, "ERROR", NULL, 0);
    OSI_LOGI(TAG, "##########  Set LTE %sabled, ret=%d", enable ? "en" : "dis", ret);
    if (ret == 0) {
        FRAMEWORK_LOG("Network %sabled", enable ? "en" : "dis");
        return 0;
    }
    return -1;
}

/**
 * @brief Check PDP context activation status
 * @details Sends AT+CGPADDR=1 to check if PDP context has been assigned an IP address
 * @return true if PDP activated with valid IP, false otherwise
 */
static bool prv_check_pdp_status(void)
{
    char resp[128] = {0};
    int cid = 0;
    char ip[20] = {0};
    char *cmd = "AT+CGPADDR=1\r\n";
    
    int ret = atCmdSendWaitResp(cmd, 1, "+CGPADDR", 1, "ERROR", resp, sizeof(resp));
    if (ret == 0) {
        if (sscanf(resp, "+CGPADDR: %d,\"%19s\"", &cid, ip) == 2) {
            if (strlen(ip) > 0 && strcmp(ip, "0.0.0.0") != 0) {
                memcpy(g_network_ctx.dev_info.cgpaddr, ip, strlen(ip));
                FRAMEWORK_LOG("PDP activated, IP: %s", ip);
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Status check work function (executed in work queue)
 * @details Performs AT command queries and updates network state.
 *          Emits events when state transitions occur.
 * @param param Unused parameter
 */
static void prv_status_check_work(void *param)
{
    static int wifi_count = 0;

    if (!g_network_ctx.initialized) {
        return;
    }

#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (g_network_ctx.bearer == FRAMEWORK_NETWORK_BEARER_WIFI) {
        prv_handle_wifi_status(&wifi_count);
        prv_poll_wifi_config();
        return;
    }
#endif
    
    wifi_count = 0; /* Reset wifi count */
    /* Update signal strength */
    prv_update_csq();
    
    /* Check network registration status */
    int net_stat = prv_check_network_status();
    
    /* Check PDP activation status */
    bool pdp_active = prv_check_pdp_status();
    
    /* Handle state transitions */
    network_state_e old_state = g_network_ctx.state;
    FRAMEWORK_LOG("net_stat:%d, pdp_active:%d, old_state:%d",net_stat,pdp_active,old_state);
    if (net_stat && pdp_active && !g_network_ctx.dev_info.pdp_activated) {
        /* PDP just activated */
        g_network_ctx.dev_info.pdp_activated = true;
        g_network_ctx.state = NETWORK_STATE_PDP_ACTIVATED;
        
        /* Send network connected message to customer layer */
        framework_msg_t msg = {.id = MSG_NET_CONNECTED, .param1 = 0, .param2 = 0};
        framework_msg_send(&msg);
        prv_schedule_status_timer(10000);
        FRAMEWORK_LOG("Network state: PDP_ACTIVATED");
        
    } else if (net_stat && old_state == NETWORK_STATE_CONNECTING) {
        /* Network registered */
        g_network_ctx.state = NETWORK_STATE_CONNECTED;
        
        /* Send network connected message to customer layer */
        framework_msg_t msg = {.id = MSG_NET_CONNECTED, .param1 = 0, .param2 = 0};
        framework_msg_send(&msg);
        prv_schedule_status_timer(10000);
        FRAMEWORK_LOG("Network state: CONNECTED");
        
    } else if (!net_stat && (old_state == NETWORK_STATE_CONNECTED || old_state == NETWORK_STATE_PDP_ACTIVATED)) {
        /* Network disconnected (only report if previously connected) */
        g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
        g_network_ctx.dev_info.pdp_activated = false;
        
        /* Send network disconnected message to customer layer */
        framework_msg_t msg = {.id = MSG_NET_DISCONNECTED, .param1 = 0, .param2 = 0};
        framework_msg_send(&msg);
        prv_ntp_on_disconnected();
        
        FRAMEWORK_LOG("Network state: DISCONNECTED");
    } else if (!net_stat && (old_state == NETWORK_STATE_CONNECTING) && (framework_network_sim_get_status() == 0)) {
        /* Network disconnected (only report if previously connected) */
        g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
        g_network_ctx.dev_info.pdp_activated = false;
        
        /* Send network disconnected message to customer layer */
        framework_msg_t msg = {.id = MSG_NET_DISCONNECTED, .param1 = 0, .param2 = 0};
        framework_msg_send(&msg);
        prv_ntp_on_disconnected();
        
        FRAMEWORK_LOG("Network state:sim DISCONNECTED");
    }
}

/**
 * @brief Status check timer callback (only triggers work queue)
 * @details Enqueues status check work to avoid blocking timer callback
 * @param param Unused parameter
 */
static void prv_status_timer_callback(void *param)
{
    if (g_network_ctx.status_work && g_network_ctx.initialized) {
        /* Enqueue status check work to system work queue */
        osiWorkEnqueue(g_network_ctx.status_work, osiSysWorkQueueLowPriority());
    }
}

static void prv_sim_detect_work_handler(void *param)
{
    static uint8_t last_sim_status = 0xff;
    static uint8_t first_inserted = 0;

    osiThreadSleep(20/5); /* Debounce delay */
    uint8_t sim_status = GpioGetLevel(SIM_CD_PIN);
    if(sim_status == last_sim_status){
        return;
    }
    FRAMEWORK_LOG("SIM card status changed: %s", sim_status == 0 ? "removed" : "inserted");

    if(framework_network_get_bearer() == FRAMEWORK_NETWORK_BEARER_WIFI){
        // framework_msg_t msg = {.id = MSG_SIM_DETECT_CHANGED, .param1 = sim_status, .param2 = 0};
        // if(sim_status == 0){
        //     msg.param2 = 0;
        // }else{
        //     msg.param2 = 1;
        // }
        first_inserted = 0;
        // framework_msg_send(&msg);
        last_sim_status = sim_status;
        return;
    }
    
    if(sim_status == 0){
        /* SIM card removed */
        /* Send network disconnected message to customer layer */
        framework_msg_t msg = {.id = MSG_SIM_DETECT_CHANGED, .param1 = sim_status, .param2 = 0};
        framework_msg_send(&msg);
        
        atCmdSendWaitResp("AT+CFUN=0\r\n", 1, "OK", 1, "ERROR", NULL, 0);
        if(!first_inserted){
            first_inserted = 1;
        }
        g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
        g_network_ctx.dev_info.pdp_activated = false;
        last_sim_status = sim_status;
        
        FRAMEWORK_LOG("Network state: DISCONNECTED");
    } else {
        /* SIM card inserted */
        framework_msg_t msg = {.id = MSG_SIM_DETECT_CHANGED, .param1 = sim_status, .param2 = 1};
        framework_msg_send(&msg);
        if(first_inserted == 0){
            first_inserted = 1;
            atCmdSendWaitResp("AT+CFUN=0\r\n", 1, "OK", 1, "ERROR", NULL, 0);
            osiThreadSleep(1000);
        }
        atCmdSendWaitResp("AT+CFUN=1\r\n", 1, "OK", 1, "ERROR", NULL, 0);
        last_sim_status = sim_status;
        FRAMEWORK_LOG("SIM card inserted");
    }
}

static void prv_sim_cd_gpio_isr(void)
{
    osiWorkEnqueue(g_network_ctx.sim_work, osiSysWorkQueueHighPriority());
}

static void prv_sim_card_gpio_init(void){
    unsigned int mfp_spk[] = {
        //sim_cd
        MFP_REG(SIM_CD_PINMUX) | SIM_CD_PINFUN | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    drvGpioPinMuxConfig(mfp_spk);
    if(g_network_ctx.sim_work == NULL){
        g_network_ctx.sim_work = osiWorkCreate(prv_sim_detect_work_handler, NULL, NULL);
    }
    GPIOConfiguration config = {
        .pinDir = GPIO_IN_PIN,
        .initLv = 0,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_TWO_EDGE,
        .isr = prv_sim_cd_gpio_isr,
        .wu = NULL
    };
    GpioInitConfiguration(SIM_CD_PIN, config);
    GpioSetDirection(SIM_CD_PIN, GPIO_IN_PIN);
}

int framework_network_sim_get_status(void)
{
    if(!g_network_ctx.initialized){
        prv_sim_card_gpio_init();
    }
    uint8_t sim_status = GpioGetLevel(SIM_CD_PIN);
    return sim_status == 0 ? false : true;
}

int framework_network_init(void)
{
    if (g_network_ctx.initialized) {
        OSI_LOGI(TAG, "Network already initialized");
        return 0;
    }
    atCmdSendWaitResp("AT*DIALMODE=0\r\n", 1, NULL, 1, NULL, NULL, 0);
    #ifdef NETWORK_LTE_RRC_RELEASE_TIME
    char resp[128] = {0};
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+MEDCR=0,34,%d\r\n", NETWORK_LTE_RRC_RELEASE_TIME);
    atCmdSendWaitResp(cmd, 1, NULL, 1, (char*)"ERROR", resp, sizeof(resp));
    #endif
    /* Initialize device information */
    memset(&g_network_ctx.dev_info, 0, sizeof(g_network_ctx.dev_info));
    g_network_ctx.dev_info.csq = 99;

    prv_sim_card_gpio_init();
    
    /* Retrieve device information */
    prv_update_imei();
    prv_update_iccid();
    prv_update_imsi();
    
    /* Create status check work */
    g_network_ctx.status_work = osiWorkCreate(prv_status_check_work, NULL, NULL);
    if (g_network_ctx.status_work == NULL) {
        FRAMEWORK_LOG("Failed to create status work");
        return -1;
    }
    
    /* Create status check timer (check every 10 seconds) */
    g_network_ctx.status_timer = osiTimerCreate(prv_status_timer_callback, NULL);
    if (g_network_ctx.status_timer == NULL) {
        FRAMEWORK_LOG("Failed to create status timer");
        osiWorkDelete(g_network_ctx.status_work);
        return -1;
    }

    g_network_ctx.bearer_work = osiWorkCreate(prv_bearer_switch_work, NULL, NULL);
    if (g_network_ctx.bearer_work == NULL) {
        FRAMEWORK_LOG("Failed to create bearer switch work");
        osiTimerDelete(g_network_ctx.status_timer);
        osiWorkDelete(g_network_ctx.status_work);
        return -1;
    }

    g_network_ctx.ntp.timer = osiTimerCreate(prv_ntp_timer_cb, NULL);
    g_network_ctx.ntp.work = osiWorkCreate(prv_ntp_work_handler, NULL, NULL);
    g_network_ctx.ntp.interval_ms = FRAMEWORK_NTP_DEFAULT_INTERVAL_MS;
    g_network_ctx.ntp.timeout_ms = FRAMEWORK_NTP_DEFAULT_TIMEOUT_MS;
    static const char *default_ntp[] = {"pool.ntp.org", "time.google.com", "in.pool.ntp.org"};
    framework_ntp_config_t ntp_cfg = {
        .servers = {default_ntp[0], default_ntp[1], default_ntp[2]},
        .server_count = 3,
        .interval_seconds = FRAMEWORK_NTP_DEFAULT_INTERVAL_MS / 1000U,
        .timeout_ms = FRAMEWORK_NTP_DEFAULT_TIMEOUT_MS,
    };
    framework_network_ntp_configure(&ntp_cfg);
    
    g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
    g_network_ctx.initialized = true;
    g_network_ctx.bearer = FRAMEWORK_NETWORK_BEARER_LTE;
    g_network_ctx.pending_bearer = FRAMEWORK_NETWORK_BEARER_LTE;
    g_network_ctx.bearer_switch_pending = false;
    g_network_ctx.bearer_switch_rearm = false;
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    g_network_ctx.wifi_open = false;
    g_network_ctx.wifi_connected = false;
    g_network_ctx.wifi_config.mode = FRAMEWORK_WIFI_CONFIG_MODE_NONE;
    g_network_ctx.wifi_config.timeout_ms = 0;
    g_network_ctx.wifi_config.start_tick = 0;
#endif
    
    FRAMEWORK_LOG("Network initialized");
    return 0;
}

int framework_network_start(void)
{
    if (!g_network_ctx.initialized || g_network_ctx.start_network) {
        FRAMEWORK_LOG("Network not initialized");
        return -1;
    }
    
    if (g_network_ctx.bearer == FRAMEWORK_NETWORK_BEARER_WIFI) {
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
        g_network_ctx.state = g_network_ctx.wifi_connected ?
            NETWORK_STATE_CONNECTED : NETWORK_STATE_DISCONNECTED;
#else
        g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
#endif
    } else {
        g_network_ctx.state = NETWORK_STATE_CONNECTING;
    }
    
    /* Start status check timer */
    osiTimerStart(g_network_ctx.status_timer, 10000);  /* 10 seconds */
    
    FRAMEWORK_LOG("Network starting (%s)...",
                  g_network_ctx.bearer == FRAMEWORK_NETWORK_BEARER_WIFI ? "WiFi" : "LTE");
    
    /* Trigger status check immediately */
    if (g_network_ctx.status_work) {
        osiWorkEnqueue(g_network_ctx.status_work, osiSysWorkQueueLowPriority());
    }
    
    g_network_ctx.start_network = true;

    return 0;
}

int framework_network_start_set(void)
{
    if (!g_network_ctx.initialized || g_network_ctx.start_network) {
        FRAMEWORK_LOG("Network not initialized");
        return -1;
    }

    g_network_ctx.start_network = true;

    return 0;
}

int framework_network_stop(void)
{
    if (!g_network_ctx.initialized) {
        return -1;
    }
    
    /* Stop timer */
    if (g_network_ctx.status_timer) {
        osiTimerStop(g_network_ctx.status_timer);
    }
    
    /* Cancel pending work */
    if (g_network_ctx.status_work) {
        osiWorkCancel(g_network_ctx.status_work);
    }

    prv_ntp_stop();
    
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    framework_network_wifi_config_stop();
    if (g_network_ctx.bearer == FRAMEWORK_NETWORK_BEARER_WIFI) {
        framework_network_wifi_close();
    }
#endif

    g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
    g_network_ctx.dev_info.pdp_activated = false;
    
    FRAMEWORK_LOG("Network stopped");
    return 0;
}

network_state_e framework_network_get_state(void)
{
    return g_network_ctx.state;
}

bool framework_network_is_connected(void)
{
    return (g_network_ctx.state == NETWORK_STATE_CONNECTED ||
            g_network_ctx.state == NETWORK_STATE_PDP_ACTIVATED);
}

bool framework_network_is_pdp_activated(void)
{
    return g_network_ctx.dev_info.pdp_activated;
}

int framework_network_get_csq(void)
{
    return g_network_ctx.dev_info.csq;
}

const char* framework_network_get_imei(void)
{
    return g_network_ctx.dev_info.imei;
}

const char* framework_network_get_iccid(void)
{
    return g_network_ctx.dev_info.iccid;
}

const char* framework_network_get_cgpaddr(void)
{
    return g_network_ctx.dev_info.cgpaddr;
}

int framework_network_get_lac(void)
{
    return g_network_ctx.dev_info.lac;
}

framework_network_bearer_e framework_network_get_bearer(void)
{
    return g_network_ctx.bearer;
}

int framework_network_switch_bearer_sync(framework_network_bearer_e bearer)
{
    OSI_LOGI(TAG, "Switching bearer to %s current=%s",
             bearer == FRAMEWORK_NETWORK_BEARER_LTE ? "LTE" : "WiFi",
             g_network_ctx.bearer == FRAMEWORK_NETWORK_BEARER_LTE ? "LTE" : "WiFi");

    if (bearer == g_network_ctx.bearer) {
        return 0;
    }

    if (bearer == FRAMEWORK_NETWORK_BEARER_LTE) {
        g_network_ctx.state = NETWORK_STATE_CONNECTING;
        g_network_ctx.dev_info.pdp_activated = false;
        prv_network_LTE_set_enabled(true);
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
        framework_network_wifi_config_stop();
        framework_network_wifi_close();
#endif
        g_network_ctx.bearer = FRAMEWORK_NETWORK_BEARER_LTE;
        if (g_network_ctx.status_work) {
            osiWorkEnqueue(g_network_ctx.status_work, osiSysWorkQueueLowPriority());
        }
        if (g_network_ctx.status_timer) {
            osiTimerStart(g_network_ctx.status_timer, 1000);
        }
        OSI_LOGI(TAG, "Switched bearer to LTE");
        return 0;
    } else if (bearer == FRAMEWORK_NETWORK_BEARER_WIFI) {
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
        g_network_ctx.dev_info.pdp_activated = false;
        g_network_ctx.state = g_network_ctx.wifi_connected ?
            NETWORK_STATE_CONNECTED : NETWORK_STATE_DISCONNECTED;
        int ret = framework_network_wifi_open();
        if (ret != 0) {
            OSI_LOGE(TAG, "WiFi open failed: %d", ret); 
            return ret;
        }
        OSI_LOGI(TAG, "WiFi open success");

        g_network_ctx.bearer = FRAMEWORK_NETWORK_BEARER_WIFI;
        prv_network_LTE_set_enabled(false);
        
        if (g_network_ctx.status_work) {
            osiWorkEnqueue(g_network_ctx.status_work, osiSysWorkQueueLowPriority());
        }
        if (g_network_ctx.status_timer) {
            osiTimerStart(g_network_ctx.status_timer, 1000);
        }
        OSI_LOGI(TAG, "Switched bearer to WiFi");
        return 0;
#else
        (void)bearer;
        return -1;
#endif
    }

    return -1;
}

int framework_network_switch_bearer(framework_network_bearer_e bearer)
{
    if (!g_network_ctx.initialized) {
        OSI_LOGE(TAG, "Network not initialized");
        return -1;
    }

    if (bearer == g_network_ctx.bearer && !g_network_ctx.bearer_switch_pending) {
        return 0;
    }

    if (g_network_ctx.bearer_work == NULL) {
        OSI_LOGE(TAG, "Bearer switch work not ready");
        return -1;
    }

    g_network_ctx.pending_bearer = bearer;
    if (!g_network_ctx.bearer_switch_pending) {
        g_network_ctx.bearer_switch_pending = true;
        osiWorkEnqueue(g_network_ctx.bearer_work, osiSysWorkQueueLowPriority());
    } else {
        g_network_ctx.bearer_switch_rearm = true;
        OSI_LOGI(TAG, "Bearer switch already pending, updated target to %s", bearer == FRAMEWORK_NETWORK_BEARER_LTE ? "LTE" : "WiFi");
    }
    return 0;
}

int framework_network_wifi_open(void)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (wifi_is_open() == 1) {
        g_network_ctx.wifi_open = true;
        return 0;
    }

    int ret = wifi_open();
    if (ret == 0) {
        OSI_LOGI(TAG, "WiFi open success");
        g_network_ctx.wifi_open = true;
    } else {
        OSI_LOGE(TAG, "WiFi open failed: %d", ret);
    }
    return ret;
#else
    return -1;
#endif
}

int framework_network_wifi_close(void)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    framework_network_wifi_config_stop();

    if (wifi_is_open() == 0) {
        g_network_ctx.wifi_open = false;
        g_network_ctx.wifi_connected = false;
        return 0;
    }

    if (wifi_is_connected()) {
        wifi_disconnect();
    }

    wifi_close();
    g_network_ctx.wifi_open = false;
    g_network_ctx.wifi_connected = false;
    // if (g_network_ctx.bearer == FRAMEWORK_NETWORK_BEARER_WIFI) {
    //     g_network_ctx.state = NETWORK_STATE_DISCONNECTED;
    // }
    return 0;
#else
    return -1;
#endif
}

bool framework_network_wifi_is_open(void)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    return wifi_is_open() == 1;
#else
    return false;
#endif
}

bool framework_network_wifi_is_connected(void)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    return (wifi_is_connected() == 1);
#else
    return false;
#endif
}

bool framework_network_wifi_connected(void)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    return g_network_ctx.wifi_connected;
#else
    return false;
#endif
}

int framework_network_wifi_config_start(const framework_wifi_config_request_t *request)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (request == NULL || request->mode == FRAMEWORK_WIFI_CONFIG_MODE_NONE) {
        return -1;
    }

    framework_network_wifi_config_stop();

    if (g_network_ctx.wifi_config.mode != FRAMEWORK_WIFI_CONFIG_MODE_NONE) {
        FRAMEWORK_LOG("WiFi provisioning already running");
        return -2;
    }

    int ret = framework_network_wifi_open();
    if (ret != 0) {
        return ret;
    }

    switch (request->mode) {
    case FRAMEWORK_WIFI_CONFIG_MODE_WEB_AP: {
        ap_mode_info_t ap_info;
        memset(&ap_info, 0, sizeof(ap_info));
        prv_prepare_softap_info(request, &ap_info);
        int enable = 1;
        wifi_set_opt(WIFI_OPT_APWEBDISABLE, &enable, sizeof(enable));
        wifi_disconnect();
        ret = wifi_ap_mode_start(&ap_info);
        if (ret != 0) {
            return ret;
        }
        break;
    }
    case FRAMEWORK_WIFI_CONFIG_MODE_SMARTCONFIG:
        ret = wifi_airkiss_start();
        if (ret != 0) {
            return ret;
        }
        break;
    default:
        return -3;
    }

    uint32_t timeout_ms = request->timeout_ms;
    if (timeout_ms == 0) {
        timeout_ms = 180000; /* default 3 minutes */
    }

    g_network_ctx.wifi_config.mode = request->mode;
    g_network_ctx.wifi_config.timeout_ms = timeout_ms;
    g_network_ctx.wifi_config.start_tick = osiGetTicks();
    prv_schedule_status_timer(1000);
    return 0;
#else
    (void)request;
    return -1;
#endif
}

int framework_network_wifi_config_stop(void)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (g_network_ctx.wifi_config.mode == FRAMEWORK_WIFI_CONFIG_MODE_NONE) {
        return 0;
    }

    if (g_network_ctx.wifi_config.mode == FRAMEWORK_WIFI_CONFIG_MODE_WEB_AP) {
        wifi_ap_mode_stop();
    } else if (g_network_ctx.wifi_config.mode == FRAMEWORK_WIFI_CONFIG_MODE_SMARTCONFIG) {
        if (wifi_is_airkiss_stop()) {
            wifi_airkiss_stop();
        }
    }

    g_network_ctx.wifi_config.mode = FRAMEWORK_WIFI_CONFIG_MODE_NONE;
    g_network_ctx.wifi_config.timeout_ms = 0;
    g_network_ctx.wifi_config.start_tick = 0;
    prv_schedule_status_timer(10000);
    return 0;
#else
    return -1;
#endif
}

int framework_network_wifi_config_poll(framework_wifi_config_result_t *result)
{
#ifdef CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
    if (g_network_ctx.wifi_config.mode == FRAMEWORK_WIFI_CONFIG_MODE_NONE) {
        return -1;
    }

    framework_wifi_config_mode_e mode = g_network_ctx.wifi_config.mode;

    if (g_network_ctx.wifi_config.timeout_ms > 0 &&
        g_network_ctx.wifi_config.start_tick != 0) {
        if (prv_elapsed_ms(g_network_ctx.wifi_config.start_tick) >=
            g_network_ctx.wifi_config.timeout_ms) {
            framework_network_wifi_config_stop();
            if (result) {
                memset(result, 0, sizeof(*result));
                result->mode = mode;
                result->success = false;
            }
            return -2;
        }
    }

    char ssid[33] = {0};
    char password[65] = {0};
    int done = (mode == FRAMEWORK_WIFI_CONFIG_MODE_WEB_AP) ?
                wifi_ap_mode_is_done(ssid, password) :
                wifi_airkiss_is_done(ssid, password);

    if (done == 1) {
        if (result) {
            memset(result, 0, sizeof(*result));
            result->mode = mode;
            result->success = true;
            strncpy(result->ssid, ssid, sizeof(result->ssid) - 1);
            strncpy(result->password, password, sizeof(result->password) - 1);
        }
        framework_network_wifi_config_stop();
        return 1;
    }

    if (done == 0) {
        if (result) {
            memset(result, 0, sizeof(*result));
            result->mode = mode;
            result->success = false;
        }
        return 0;
    }

    if (result) {
        memset(result, 0, sizeof(*result));
        result->mode = mode;
        result->success = false;
    }
    framework_network_wifi_config_stop();
    return done;
#else
    (void)result;
    return -1;
#endif
}

int framework_network_set_apn(const char *apn, const char *username, const char *password)
{
    if (!apn || strlen(apn) == 0 || strlen(apn) > 63) {
        OSI_LOGI(TAG, "Invalid APN parameter");
        return -1;
    }
    
    char cmd[256] = {0};
    char resp[128] = {0};
    int ret;

    /* If PDP is active, deactivate before changing APN */
    bool pdp_active = prv_check_pdp_status();
    if (pdp_active) {
        memset(cmd, 0, sizeof(cmd));
        snprintf(cmd, sizeof(cmd), "AT+CGACT=0,1\r\n");
        ret = atCmdSendWaitResp(cmd, 10, "OK", 1, "ERROR", resp, sizeof(resp));
        if (ret != 0) {
            OSI_LOGI(TAG, "Failed to deactivate PDP before APN switch");
            return -1;
        }
        g_network_ctx.dev_info.pdp_activated = false;
    }
    
    // Set PDP context with APN
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", apn);
    ret = atCmdSendWaitResp(cmd, 10, "OK", 1, "ERROR", resp, sizeof(resp));
    
    if (ret != 0) {
        OSI_LOGI(TAG, "Failed to set APN: %s", apn);
        return -1;
    }
    
    // If username and password provided, set authentication
    if (username && password && strlen(username) > 0 && strlen(password) > 0) {
        memset(cmd, 0, sizeof(cmd));
        memset(resp, 0, sizeof(resp));
        snprintf(cmd, sizeof(cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"\r\n", username, password);
        ret = atCmdSendWaitResp(cmd, 10, "OK", 1, "ERROR", resp, sizeof(resp));
        
        if (ret != 0) {
            OSI_LOGI(TAG, "Failed to set APN authentication");
            return -1;
        }
    }
    
    
    // Store APN in context
    strncpy(g_network_ctx.dev_info.apn, apn, sizeof(g_network_ctx.dev_info.apn) - 1);
    g_network_ctx.dev_info.apn[sizeof(g_network_ctx.dev_info.apn) - 1] = '\0';
    
    OSI_LOGI(TAG, "APN configured: %s", apn);

    /* Reactivate PDP if we deactivated it */
    if (pdp_active) {
        memset(cmd, 0, sizeof(cmd));
        memset(resp, 0, sizeof(resp));
        snprintf(cmd, sizeof(cmd), "AT+CGACT=1,1\r\n");
        ret = atCmdSendWaitResp(cmd, 10, "OK", 1, "ERROR", resp, sizeof(resp));
        if (ret != 0) {
            OSI_LOGI(TAG, "Failed to reactivate PDP after APN switch");
            return -1;
        }
        g_network_ctx.dev_info.pdp_activated = true;
    }
    return 0;
}

int framework_network_get_apn(char *apn, int max_len)
{
    if (!apn || max_len < 1) {
        return -1;
    }
    
    if (strlen(g_network_ctx.dev_info.apn) > 0) {
        strncpy(apn, g_network_ctx.dev_info.apn, max_len - 1);
        apn[max_len - 1] = '\0';
        return 0;
    }
    
    // Try to query from modem
    char cmd[32] = "AT+CGDCONT?\r\n";
    char resp[256] = {0};
    int ret = atCmdSendWaitResp(cmd, 5, "+CGDCONT:", 1, "ERROR", resp, sizeof(resp));
    
    if (ret == 0 && strlen(resp) > 0) {
        // Parse response: +CGDCONT: 1,"IP","apn_name",...
        char *p = strstr(resp, "+CGDCONT:");
        if (p) {
            // Skip to third field (APN)
            p = strchr(p, '"');
            if (p) p = strchr(p + 1, '"');
            if (p) p = strchr(p + 1, '"');
            if (p) {
                p++;
                char *end = strchr(p, '"');
                if (end) {
                    int len = end - p;
                    if (len >= max_len) len = max_len - 1;
                    strncpy(apn, p, len);
                    apn[len] = '\0';
                    
                    // Store in context
                    strncpy(g_network_ctx.dev_info.apn, apn, sizeof(g_network_ctx.dev_info.apn) - 1);
                    g_network_ctx.dev_info.apn[sizeof(g_network_ctx.dev_info.apn) - 1] = '\0';
                    
                    return 0;
                }
            }
        }
    }
    
    apn[0] = '\0';
    return -1;
}

int framework_network_ntp_configure(const framework_ntp_config_t *config)
{
    if (config == NULL || config->server_count == 0 || config->server_count > FRAMEWORK_NTP_MAX_SERVERS) {
        return -1;
    }

    memset(g_network_ctx.ntp.servers, 0, sizeof(g_network_ctx.ntp.servers));
    size_t copied = 0;
    for (size_t i = 0; i < config->server_count && i < FRAMEWORK_NTP_MAX_SERVERS; i++) {
        const char *src = config->servers[i];
        if (src && src[0] != '\0') {
            strncpy(g_network_ctx.ntp.servers[copied], src, sizeof(g_network_ctx.ntp.servers[copied]) - 1);
            g_network_ctx.ntp.servers[copied][sizeof(g_network_ctx.ntp.servers[copied]) - 1] = '\0';
            copied++;
        }
    }

    if (copied == 0) {
        return -1;
    }

    g_network_ctx.ntp.server_count = copied;
    g_network_ctx.ntp.current_index = 0;

    uint32_t interval_ms = config->interval_seconds ? (config->interval_seconds * 1000U) : FRAMEWORK_NTP_DEFAULT_INTERVAL_MS;
    if (interval_ms < FRAMEWORK_NTP_MIN_INTERVAL_MS) {
        interval_ms = FRAMEWORK_NTP_MIN_INTERVAL_MS;
    }
    g_network_ctx.ntp.interval_ms = interval_ms;
    g_network_ctx.ntp.timeout_ms = config->timeout_ms ? config->timeout_ms : FRAMEWORK_NTP_DEFAULT_TIMEOUT_MS;

    return 0;
}

int framework_network_ntp_sync_now(bool sync)
{
    OSI_LOGI("Test-Time", "[framework_network_ntp_sync_now] ENTER: sync=%d, server_count=%u, is_connected=%d",
             sync, (unsigned int)g_network_ctx.ntp.server_count, framework_network_is_connected());

    for (size_t i = 0; i < g_network_ctx.ntp.server_count; i++) {
        OSI_LOGI("Test-Time", "[framework_network_ntp_sync_now] Server[%u]='%s'",
                 (unsigned int)i, g_network_ctx.ntp.servers[i]);
    }

    if (g_network_ctx.ntp.server_count == 0 || g_network_ctx.ntp.work == NULL) {
        OSI_LOGE("Test-Time", "[framework_network_ntp_sync_now] Abort: server_count=0 or work=NULL");
        return -1;
    }

    /* Async path (default): enqueue work like before */
    if (!sync) {
        OSI_LOGI("Test-Time", "[framework_network_ntp_sync_now] Async mode requested: enqueuing work to osiSysWorkQueueLowPriority()");
        osiWorkEnqueue(g_network_ctx.ntp.work, osiSysWorkQueueLowPriority());
        return 0;
    }

    /* Sync path: perform immediate NTP sync on current server list */
    OSI_LOGI("Test-Time", "[framework_network_ntp_sync_now] Synchronous mode requested: executing immediate sync");
    if (!framework_network_is_connected()) {
        OSI_LOGW("Test-Time", "[framework_network_ntp_sync_now] NTP sync skipped: network not connected");
        return -1;
    }

    size_t idx = g_network_ctx.ntp.current_index % g_network_ctx.ntp.server_count;
    framework_ntp_result_e last_err = FRAMEWORK_NTP_ERR_BAD_RESPONSE;
    time_t ts = 0;

    for (size_t attempts = 0; attempts < g_network_ctx.ntp.server_count; attempts++) {
        const char *server = g_network_ctx.ntp.servers[idx];
        OSI_LOGI("Test-Time", "[framework_network_ntp_sync_now] Sync attempt %u/%u with server[%u]='%s'",
                 (unsigned int)(attempts + 1), (unsigned int)g_network_ctx.ntp.server_count,
                 (unsigned int)idx, server ? server : "(null)");

        framework_ntp_result_e ret = prv_ntp_sync_one(server, g_network_ctx.ntp.timeout_ms, &ts);
        if (ret == FRAMEWORK_NTP_RESULT_OK) {
            OSI_LOGI("Test-Time", "[framework_network_ntp_sync_now] Sync SUCCESS from server '%s', ts=%ld! Applying time...",
                     server, (long)ts);
            prv_ntp_apply_time(ts);
            g_network_ctx.ntp.current_index = idx;

            framework_msg_t msg = {
                .id = MSG_NET_NTP_SUCCESS,
                .param1 = (uint32_t)idx,
                .param2 = (uint32_t)ts,
                .param3 = 0,
            };
            OSI_LOGI("Test-Time", "[framework_network_ntp_sync_now] Sending MSG_NET_NTP_SUCCESS (idx=%u, ts=%ld)",
                     (unsigned int)idx, (long)ts);
            framework_msg_send(&msg);
            prv_ntp_schedule(g_network_ctx.ntp.interval_ms);
            return 0;
        }

        OSI_LOGW("Test-Time", "[framework_network_ntp_sync_now] Server '%s' failed, ret=%d", server, ret);
        last_err = ret;
        idx = (idx + 1) % g_network_ctx.ntp.server_count;
    }

    /* All servers failed */
    size_t failed_idx = (idx + g_network_ctx.ntp.server_count - 1) % g_network_ctx.ntp.server_count;
    OSI_LOGE("Test-Time", "[framework_network_ntp_sync_now] All %u NTP servers failed synchronously! Sending MSG_NET_NTP_FAILURE (last_err=%d)",
             (unsigned int)g_network_ctx.ntp.server_count, last_err);
    framework_msg_t msg = {
        .id = MSG_NET_NTP_FAILURE,
        .param1 = (uint32_t)failed_idx,
        .param2 = (uint32_t)last_err,
        .param3 = 0,
    };
    framework_msg_send(&msg);
    prv_ntp_schedule(g_network_ctx.ntp.interval_ms);
    return -1;
}
int framework_set_apweb_ssid(char *ssid)
{
    if(ssid == NULL || strlen(ssid) == 0 || strlen(ssid) > 32) {
        OSI_LOGI(TAG, "Invalid SSID parameter");
        return -1;
    }

    memcpy(g_network_ctx.dev_info.apweb_ssid, ssid, sizeof(g_network_ctx.dev_info.apweb_ssid));

    return 0;
}