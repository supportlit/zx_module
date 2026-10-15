/**
 * @file customer_status_report.c
 * @brief template status reporting implementation
 */

#include "customer_status_report.h"
#include "framework_mqtt.h"
#include "framework_log.h"
#include "framework_battery.h"
#include "framework_network.h"
#include "framework_device_config.h"
#include "app_version.h"
#include "cJSON.h"
#include "osi_api.h"
#include "customer_config.h"
#include "framework_http.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TAG "STATUS_REPORT"

#define TOPIC_SOUNDBOX_STATUS           "BP1/soundbox/status"
#define TOPIC_CHARGING_STATUS           "BP1/soundbox/charging-status"
#define TOPIC_DEVICE_COORDINATE         "BP1/soundbox/coordinate"
#define TOPIC_PAYMENT_CALLBACK          "BP1/payment/callback"

#define STATUS_REPORT_STACK_SIZE        (4 * 1024)
#define STATUS_REPORT_PRIORITY          150
#define JAKARTA_UTC_OFFSET_SEC          (7 * 3600)
static const int k_heartbeat_hours[] = {3, 15, 19};
static uint8_t ntp_sync = 0;

typedef struct {
    uint8_t initialized;
    uint8_t running;
    uint32_t report_interval_sec;
    osiThread_t *report_thread;
} status_report_ctx_t;

static status_report_ctx_t g_report_ctx = {0};

static const char* prv_get_connection_type_string(connection_type_e type)
{
    switch (type) {
        case CONNECTION_TYPE_4G:
            return "4G";
        case CONNECTION_TYPE_WIFI:
            return "WIFI";
        default:
            return "UNKNOWN";
    }
}

static const char* prv_get_device_status_string(device_status_e status)
{
    switch (status) {
        case DEVICE_STATUS_ONLINE:
            return "ONLINE";
        case DEVICE_STATUS_OFFLINE:
            return "OFFLINE";
        case DEVICE_STATUS_IDLE:
            return "IDLE";
        default:
            return "UNKNOWN";
    }
}

static const char* prv_get_charging_status_string(charging_status_e status)
{
    switch (status) {
        case CHARGING_STATUS_PLUG:
            return "plug";
        case CHARGING_STATUS_UNPLUG:
            return "unplug";
        default:
            return "unknown";
    }
}

static int prv_publish_json(const char *topic, cJSON *json)
{
    if (!topic || !json) {
        return -1;
    }

    char *payload = cJSON_PrintUnformatted(json);
    if (!payload) {
        OSI_LOGE(TAG, "Failed to serialize JSON");
        return -2;
    }

    int ret = framework_mqtt_publish(topic, payload, strlen(payload), 1, 0);
    if (ret != 0) {
        OSI_LOGE(TAG, "Failed to publish to %s: %d", topic, ret);
    } else {
        OSI_LOGI(TAG, "Published to %s: %s", topic, payload);
    }

    cJSON_free(payload);
    return ret;
}

static void prv_collect_and_report_status(void)
{
    soundbox_status_t status = {0};
    
    /* Get serial number */
    framework_device_config_get_sn(status.serial_number, sizeof(status.serial_number));
    
    /* Get battery level */
    status.battery = framework_battery_get_percent();
    
    /* Get signal strength (CSQ: 0-31, 99=unknown) */
    int csq = framework_network_get_csq();
    if (csq >= 20 && csq <= 31) {
        strcpy(status.signal, "strong");
    } else if (csq >= 10 && csq < 20) {
        strcpy(status.signal, "medium");
    } else if (csq >= 0 && csq < 10) {
        strcpy(status.signal, "weak");
    } else {
        strcpy(status.signal, "unknown");
    }
    
    /* Get device status */
    if (framework_mqtt_is_connected()) {
        status.status = DEVICE_STATUS_ONLINE;
    } else {
        status.status = DEVICE_STATUS_OFFLINE;
    }
    
    /* Get firmware version */
    strncpy(status.firm_ver, APP_VERSION_STRING, sizeof(status.firm_ver) - 1);
    
    /* Get connection type */
    if (framework_network_wifi_is_connected()) {
        status.connection_type = CONNECTION_TYPE_WIFI;
    } else if (framework_network_is_connected()) {
        status.connection_type = CONNECTION_TYPE_4G;
    } else {
        status.connection_type = CONNECTION_TYPE_UNKNOWN;
    }
    
    customer_status_report_soundbox(&status);
}

/* Compute delay (ms) until next heartbeat at 03:00 / 15:00 / 19:00 UTC+7 */
static uint32_t prv_calc_next_heartbeat_delay_ms(void)
{
    time_t now_utc = time(NULL);
    if (now_utc == (time_t)-1) {
        return HEARTBEAT_INTERVAL_SEC * 1000U; /* Fallback to legacy interval */
    }
    time_t now_jakarta = now_utc + JAKARTA_UTC_OFFSET_SEC;
    OSI_LOGI(TAG, "now_utc:%ld,,Current UTC time: %ld(%s)", now_utc, now_jakarta, ctime(&now_jakarta));
    if(osiGetTimeZone() != 8){osiSetTimeZoneMin(8*60);}
    struct tm *tm_j = localtime(&now_jakarta);
    if (!tm_j) {
        return HEARTBEAT_INTERVAL_SEC * 1000U;
    }
    OSI_LOGI(TAG, "Current UTC time: %d-%d-%d-%d:%d:%d", tm_j->tm_year+1900,tm_j->tm_mon+1,tm_j->tm_mday,tm_j->tm_hour,tm_j->tm_min,tm_j->tm_sec);
    int sod = tm_j->tm_hour * 3600 + tm_j->tm_min * 60 + tm_j->tm_sec; /* seconds of day */
    int next_sod = -1;
    for (size_t i = 0; i < sizeof(k_heartbeat_hours)/sizeof(k_heartbeat_hours[0]); ++i) {
        int candidate = k_heartbeat_hours[i] * 3600; /* on the hour */
        if (candidate > sod) {
            next_sod = candidate;
            break;
        }
    }
    if (next_sod < 0) {
        next_sod = k_heartbeat_hours[0] * 3600 + 24 * 3600; /* first slot next day */
    }

    int delta_sec = next_sod - sod;
    if (delta_sec <= 0) {
        delta_sec += 24 * 3600;
    }
    return (uint32_t)delta_sec * 1000U;
}

static void prv_status_report_thread(void *param)
{
    (void)param;

    OSI_LOGI(TAG, "Status report thread started (scheduled 03:00/15:00/19:00 UTC+7)");
    while (g_report_ctx.running) {

        framework_network_ntp_sync_now(false);
        while(!ntp_sync){
            osiThreadSleep(1000/5);
        }
        ntp_sync = 0;
        uint32_t delay_ms = prv_calc_next_heartbeat_delay_ms();
        OSI_LOGI(TAG, "Next heartbeat in %ld sec", delay_ms / 1000U);
        osiThreadSleep(delay_ms/5);

        if (!g_report_ctx.running) {
            break;
        }

        if (framework_mqtt_is_connected()) {
            prv_collect_and_report_status();
        } else {
            OSI_LOGW(TAG, "MQTT not connected, skipping scheduled heartbeat");
        }
    }

    OSI_LOGI(TAG, "Status report thread stopped");
}

int customer_status_report_init(void)
{
    if (g_report_ctx.initialized) {
        OSI_LOGI(TAG, "Already initialized");
        return 0;
    }
    
    memset(&g_report_ctx, 0, sizeof(status_report_ctx_t));
    g_report_ctx.initialized = 1;
    
    OSI_LOGI(TAG, "Status report module initialized");
    return 0;
}

int customer_status_report_deinit(void)
{
    if (!g_report_ctx.initialized) {
        return 0;
    }
    
    customer_status_report_stop();
    
    memset(&g_report_ctx, 0, sizeof(status_report_ctx_t));
    
    OSI_LOGI(TAG, "Status report module deinitialized");
    return 0;
}

int customer_status_report_start(uint32_t interval_sec)
{
    if (!g_report_ctx.initialized) {
        OSI_LOGE(TAG, "Not initialized");
        return -1;
    }
    
    if (interval_sec == 0) {
        return customer_status_report_stop();
    }
    
    if (g_report_ctx.running) {
        OSI_LOGW(TAG, "Already running");
        return 0;
    }
    
    g_report_ctx.report_interval_sec = interval_sec;
    g_report_ctx.running = 1;
    
    g_report_ctx.report_thread = osiThreadCreate("status_report",
                                                 prv_status_report_thread,
                                                 NULL,
                                                 STATUS_REPORT_PRIORITY,
                                                 STATUS_REPORT_STACK_SIZE);
    if (!g_report_ctx.report_thread) {
        OSI_LOGE(TAG, "Failed to create report thread");
        g_report_ctx.running = 0;
        return -2;
    }
    
    OSI_LOGI(TAG, "Status report started, interval=%d sec", interval_sec);
    return 0;
}

int customer_status_report_stop(void)
{
    if (!g_report_ctx.initialized || !g_report_ctx.running) {
        return 0;
    }
    
    g_report_ctx.running = 0;
    
    /* Wait for thread to exit */
    if (g_report_ctx.report_thread) {
        osiThreadSleep(100);
        g_report_ctx.report_thread = NULL;
    }
    
    OSI_LOGI(TAG, "Status report stopped");
    return 0;
}

int customer_status_report_soundbox(const soundbox_status_t *status)
{
    if (!status) {
        return -1;
    }
    
    if (!framework_mqtt_is_connected()) {
        OSI_LOGW(TAG, "MQTT not connected");
        return -2;
    }
    
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return -3;
    }
    
    cJSON_AddStringToObject(json, "serial_number", status->serial_number);
    cJSON_AddNumberToObject(json, "battery", status->battery);
    cJSON_AddStringToObject(json, "signal", status->signal);
    cJSON_AddStringToObject(json, "status", prv_get_device_status_string(status->status));
    cJSON_AddStringToObject(json, "firm_ver", status->firm_ver);
    cJSON_AddStringToObject(json, "connection_type", prv_get_connection_type_string(status->connection_type));
    
    int ret = prv_publish_json(TOPIC_SOUNDBOX_STATUS, json);
    cJSON_Delete(json);
    
    return ret;
}

int customer_status_report_charging(const charging_status_t *status)
{
    if (!status) {
        return -1;
    }
    
    if (!framework_mqtt_is_connected()) {
        OSI_LOGW(TAG, "MQTT not connected");
        return -2;
    }
    
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return -3;
    }
    
    cJSON_AddStringToObject(json, "serial_number", status->serial_number);
    cJSON_AddNumberToObject(json, "battery", status->battery);
    cJSON_AddStringToObject(json, "status", prv_get_charging_status_string(status->status));
    
    int ret = prv_publish_json(TOPIC_CHARGING_STATUS, json);
    cJSON_Delete(json);
    
    return ret;
}

int customer_status_report_coordinate(const device_coordinate_t *coord)
{
    if (!coord) {
        return -1;
    }
    
    if (!framework_mqtt_is_connected()) {
        OSI_LOGW(TAG, "MQTT not connected");
        return -2;
    }
    
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return -3;
    }
    
    cJSON_AddStringToObject(json, "serial_number", coord->serial_number);
    cJSON_AddNumberToObject(json, "lat", coord->lat);
    cJSON_AddNumberToObject(json, "long", coord->longitude);  /* Note: JSON uses "long" not "longitude" */
    
    int ret = prv_publish_json(TOPIC_DEVICE_COORDINATE, json);
    cJSON_Delete(json);
    
    return ret;
}

int customer_status_report_payment(const payment_callback_t *callback)
{
    if (!callback) {
        return -1;
    }
    
    if (!framework_mqtt_is_connected()) {
        OSI_LOGW(TAG, "MQTT not connected");
        return -2;
    }
    
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return -3;
    }
    
    cJSON_AddStringToObject(json, "serial_number", callback->serial_number);
    cJSON_AddStringToObject(json, "transaction_id", callback->transaction_id);
    cJSON_AddStringToObject(json, "status", callback->status);
    
    int ret = prv_publish_json(TOPIC_PAYMENT_CALLBACK, json);
    cJSON_Delete(json);
    
    return ret;
}

int customer_set_report_ntp_sync(uint8_t status)
{
    ntp_sync = status;
    return 0;
}

/* ============================================================================
 * TMS HTTPS Heartbeat Implementation
 * ============================================================================ */

static osiThread_t *s_tms_hb_thread_handle = NULL;
static uint32_t s_tms_hb_interval_sec = TMS_HEARTBEAT_INTERVAL_SEC;
static volatile uint8_t s_tms_hb_running = 0;

static void prv_tms_hb_http_callback(http_response_t *response, void *user_data)
{
    (void)user_data;
    if (!response) {
        OSI_LOGE(TAG, "[TMS_HB] No response received");
        return;
    }

    if (response->result == HTTP_RESULT_SUCCESS && response->status_code == 200) {
        OSI_LOGI(TAG, "[TMS_HB] Heartbeat SUCCESS [HTTP %d, len=%u]: %.64s",
                 response->status_code,
                 (unsigned int)response->response_len,
                 response->response_data ? response->response_data : "");
    } else {
        OSI_LOGE(TAG, "[TMS_HB] Heartbeat FAILED [Result: %d, HTTP Code: %d]",
                 response->result, response->status_code);
    }
}

int customer_send_tms_heartbeat(void)
{
    if (!framework_network_is_connected()) {
        OSI_LOGW(TAG, "[TMS_HB] Network not connected, skipping heartbeat");
        return -1;
    }

    char sn[32] = {0};
    framework_device_config_get_sn(sn, sizeof(sn));
    if (strlen(sn) == 0) {
        strncpy(sn, TMS_HEARTBEAT_TID, sizeof(sn) - 1);
    }

    const char *iccid = framework_network_get_iccid();
    if (!iccid) {
        iccid = "";
    }

    char act_date[36] = {0};
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info && tm_info->tm_year > 120) { /* year > 2020 */
        snprintf(act_date, sizeof(act_date), "%04d/%02d/%02d %02d:%02d:%02d +22 0",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else {
        strncpy(act_date, "2026/09/24 18:48:13 +22 0", sizeof(act_date) - 1);
    }

    /* Allocate JSON body on heap to avoid stack overflow */
    char *json_body = (char *)malloc(1024);
    if (!json_body) {
        OSI_LOGE(TAG, "[TMS_HB] Failed to allocate JSON buffer");
        return -2;
    }

    int len = snprintf(json_body, 1024,
        "{\n"
        "    \"actDate\": \"%s\",\n"
        "    \"actStatus\": \"%s\",\n"
        "    \"androidVersion\": \"%s\",\n"
        "    \"apkVersion\": \"%s\",\n"
        "    \"appVersion\": \"%s\",\n"
        "    \"configVersion\": \"%s\",\n"
        "    \"cpu\": \"%s\",\n"
        "    \"did\": \"%s\",\n"
        "    \"firmwareVersion\": \"%s\",\n"
        "    \"lastStan\": \"\",\n"
        "    \"lat\": \"\",\n"
        "    \"lng\": \"\",\n"
        "    \"lstTxnDateTime\": \"\",\n"
        "    \"mId\": \"%s\",\n"
        "    \"simSrlNo\": \"%s\",\n"
        "    \"srlNo\": \"%s\",\n"
        "    \"tId\": \"%s\",\n"
        "    \"tModel\": \"%s\",\n"
        "    \"tStatus\": \"%s\",\n"
        "    \"trsm\": \"\"\n"
        "}",
        act_date,
        TMS_HEARTBEAT_ACT_STATUS,
        TMS_HEARTBEAT_ANDROID_VER,
        TMS_HEARTBEAT_APK_VER,
        TMS_HEARTBEAT_APP_VER,
        TMS_HEARTBEAT_CONFIG_VER,
        TMS_HEARTBEAT_CPU,
        TMS_HEARTBEAT_DID,
        TMS_HEARTBEAT_FIRMWARE_VER,
        TMS_HEARTBEAT_MID,
        iccid,
        sn,
        TMS_HEARTBEAT_TID,
        TMS_HEARTBEAT_MODEL,
        TMS_HEARTBEAT_TSTATUS
    );

    if (len < 0 || len >= 1024) {
        OSI_LOGE(TAG, "[TMS_HB] JSON buffer overflow");
        free(json_body);
        return -3;
    }

    static const char *header = "Authorization: " TMS_HEARTBEAT_AUTH_TOKEN "\r\nContent-Type: application/json\r\n";

    OSI_LOGI(TAG, "[TMS_HB] Sending HTTPS Heartbeat to %s (len=%d)", TMS_HEARTBEAT_URL, len);

    int ret = framework_http_post_json(
        TMS_HEARTBEAT_URL,
        json_body,
        header,
        prv_tms_hb_http_callback,
        NULL
    );

    free(json_body);

    if (ret != 0) {
        OSI_LOGE(TAG, "[TMS_HB] Failed to queue HTTP post: %d", ret);
    }
    return ret;
}

static void prv_tms_hb_thread(void *param)
{
    (void)param;
    OSI_LOGI(TAG, "[TMS_HB] Thread started, interval=%u s", (unsigned int)s_tms_hb_interval_sec);

    /* Allow network stack, PDP routing, and MQTT to settle cleanly before first transmission */
    for (int i = 0; i < 5 && s_tms_hb_running; i++) {
        osiThreadSleepMs(1000);
    }

    while (s_tms_hb_running) {
        if (framework_network_is_connected()) {
            customer_send_tms_heartbeat();
        } else {
            OSI_LOGW(TAG, "[TMS_HB] Network not connected, waiting...");
        }

        /* Sleep in 1-second chunks so that customer_tms_heartbeat_stop() can exit cleanly */
        uint32_t elapsed = 0;
        while (s_tms_hb_running && elapsed < s_tms_hb_interval_sec) {
            osiThreadSleepMs(1000);
            elapsed++;
        }
    }

    s_tms_hb_thread_handle = NULL;
    OSI_LOGI(TAG, "[TMS_HB] Thread stopped");
    osiThreadExit();
}

int customer_tms_heartbeat_start(uint32_t interval_sec)
{
    if (interval_sec == 0) {
        interval_sec = TMS_HEARTBEAT_INTERVAL_SEC;
    }
    s_tms_hb_interval_sec = interval_sec;

    if (s_tms_hb_running) {
        OSI_LOGI(TAG, "[TMS_HB] Already running, interval=%u s", (unsigned int)interval_sec);
        return 0;
    }

    s_tms_hb_running = 1;
    s_tms_hb_thread_handle = osiThreadCreate("tms_hb",
                                             prv_tms_hb_thread,
                                             NULL,
                                             STATUS_REPORT_PRIORITY,
                                             4 * 1024);
    if (!s_tms_hb_thread_handle) {
        OSI_LOGE(TAG, "[TMS_HB] Failed to create thread");
        s_tms_hb_running = 0;
        return -1;
    }

    OSI_LOGI(TAG, "[TMS_HB] Heartbeat thread created, interval=%u s", (unsigned int)interval_sec);
    return 0;
}

int customer_tms_heartbeat_stop(void)
{
    if (!s_tms_hb_running) {
        return 0;
    }

    s_tms_hb_running = 0;
    OSI_LOGI(TAG, "[TMS_HB] Heartbeat stopping...");
    return 0;
}