/**
 * @file customer_pos_config.c
 * @brief POS Configuration Management Implementation
 * @details Handles fetching POS details from TMS by SN, caching in D:/template_pos_config.json,
 *          and providing in-memory configuration access.
 */

#include "customer_pos_config.h"
#include "customer_config.h"
#include "framework_http.h"
#include "framework_device_config.h"
#include "framework_network.h"
#include "framework_log.h"
#include "fs_api.h"
#include "cJSON.h"
#include "osi_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define TAG "POS_CONFIG"

#ifndef TMS_POS_CONFIG_AUTH_TOKEN
#define TMS_POS_CONFIG_AUTH_TOKEN "f1c3a7e8b9d5e2a0dh7d8f9dh97d"
#endif

#ifndef TMS_POS_CONFIG_URL_BASE
#define TMS_POS_CONFIG_URL_BASE "https://103.157.22.211:8075/sxtms/online/tms/getConfigDetailByTid"
#endif

static customer_pos_config_t g_pos_config = {0};
static bool s_fetch_in_progress = false;

/* Forward declarations */
static void prv_pos_config_http_callback(http_response_t *response, void *user_data);
static int prv_save_config_to_file(const char *json_data, uint32_t len);
static int prv_load_config_from_file(void);
static void prv_parse_json_to_struct(const char *json_str, customer_pos_config_t *cfg);

#define JSON_GET_STR(obj, key, dest) do { \
    cJSON *item = cJSON_GetObjectItem(obj, key); \
    if (item && cJSON_IsString(item) && item->valuestring) { \
        strncpy(dest, item->valuestring, sizeof(dest) - 1); \
        dest[sizeof(dest) - 1] = '\0'; \
    } \
} while(0)

#define JSON_GET_NUM(obj, key, dest) do { \
    cJSON *item = cJSON_GetObjectItem(obj, key); \
    if (item && cJSON_IsNumber(item)) { \
        dest = (uint32_t)item->valueint; \
    } \
} while(0)

static void prv_parse_json_to_struct(const char *json_str, customer_pos_config_t *cfg)
{
    if (!json_str || !cfg) {
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        OSI_LOGE(TAG, "JSON parse failed for POS config");
        return;
    }

    /* Check status field if present */
    cJSON *j_status = cJSON_GetObjectItem(root, "status");
    if (j_status && cJSON_IsString(j_status)) {
        if (strcmp(j_status->valuestring, "200") != 0) {
            OSI_LOGW(TAG, "POS config returned status: %s", j_status->valuestring);
            cJSON_Delete(root);
            return;
        }
    }

    cJSON *target_obj = NULL;
    cJSON *j_resp = cJSON_GetObjectItem(root, "responseObject");
    if (j_resp && cJSON_IsArray(j_resp) && cJSON_GetArraySize(j_resp) > 0) {
        target_obj = cJSON_GetArrayItem(j_resp, 0);
    } else if (j_resp && cJSON_IsObject(j_resp)) {
        target_obj = j_resp;
    } else {
        target_obj = root;
    }

    if (!target_obj) {
        OSI_LOGE(TAG, "No valid config object found in JSON");
        cJSON_Delete(root);
        return;
    }

    JSON_GET_NUM(target_obj, "posConfigId", cfg->pos_config_id);
    JSON_GET_STR(target_obj, "mid", cfg->mid);
    JSON_GET_STR(target_obj, "tid", cfg->tid);
    JSON_GET_STR(target_obj, "serialNumber", cfg->serial_number);
    JSON_GET_STR(target_obj, "merchantName", cfg->merchant_name);
    JSON_GET_STR(target_obj, "merchantLocation", cfg->merchant_location);
    JSON_GET_STR(target_obj, "merchantCity", cfg->merchant_city);
    JSON_GET_STR(target_obj, "merchantState", cfg->merchant_state);
    JSON_GET_STR(target_obj, "merchantPhone", cfg->merchant_phone);
    JSON_GET_STR(target_obj, "currencyCode", cfg->currency_code);
    JSON_GET_STR(target_obj, "countryCode", cfg->country_code);
    JSON_GET_STR(target_obj, "mcc", cfg->mcc);
    JSON_GET_STR(target_obj, "vpa", cfg->vpa);
    JSON_GET_STR(target_obj, "upiDeviceId", cfg->upi_device_id);
    JSON_GET_STR(target_obj, "storeNumber", cfg->store_number);
    JSON_GET_STR(target_obj, "processor", cfg->processor);
    JSON_GET_STR(target_obj, "developerId", cfg->developer_id);
    JSON_GET_STR(target_obj, "merchantType", cfg->merchant_type);
    JSON_GET_STR(target_obj, "acquirerBinId", cfg->acquirer_bin_id);
    JSON_GET_STR(target_obj, "acquirerBinTsys", cfg->acquirer_bin_tsys);
    JSON_GET_STR(target_obj, "agentBankTsys", cfg->agent_bank_tsys);
    JSON_GET_STR(target_obj, "agentChainTsys", cfg->agent_chain_tsys);
    JSON_GET_STR(target_obj, "genKeyTsys", cfg->gen_key_tsys);
    JSON_GET_STR(target_obj, "datawireUrl", cfg->datawire_url);
    JSON_GET_STR(target_obj, "qsrLimit", cfg->qsr_limit);
    JSON_GET_STR(target_obj, "userId", cfg->user_id);
    JSON_GET_STR(target_obj, "userName", cfg->user_name);

    cfg->valid = true;
    cJSON_Delete(root);
}

static int prv_save_config_to_file(const char *json_data, uint32_t len)
{
    if (!json_data || len == 0) {
        return -1;
    }

    int fd = fs_open(POS_CONFIG_FILE_PATH, "wb+");
    if (fd <= 0) {
        OSI_LOGE(TAG, "Failed to open %s for write (fd=%d)", POS_CONFIG_FILE_PATH, fd);
        return -2;
    }

    size_t written = fs_write((void *)json_data, len, 1, fd);
    fs_close(fd);

    if (written <= 0) {
        OSI_LOGE(TAG, "Failed to write %s", POS_CONFIG_FILE_PATH);
        return -3;
    }

    OSI_LOGI(TAG, "POS config successfully saved to %s (%u bytes)", POS_CONFIG_FILE_PATH, len);
    return 0;
}

static int prv_load_config_from_file(void)
{
    int flen = fs_size(POS_CONFIG_FILE_PATH);
    if (flen <= 0) {
        OSI_LOGI(TAG, "No cached POS config found at %s (size=%d)", POS_CONFIG_FILE_PATH, flen);
        return -1;
    }

    int fd = fs_open(POS_CONFIG_FILE_PATH, "rb");
    if (fd <= 0) {
        OSI_LOGE(TAG, "Failed to open %s for read (fd=%d)", POS_CONFIG_FILE_PATH, fd);
        return -2;
    }

    char *buf = (char *)malloc(flen + 1);
    if (!buf) {
        OSI_LOGE(TAG, "Failed to allocate memory for config file read");
        fs_close(fd);
        return -3;
    }

    memset(buf, 0, flen + 1);
    size_t read_bytes = fs_read(buf, flen, 1, fd);
    fs_close(fd);

    if (read_bytes <= 0) {
        OSI_LOGE(TAG, "Failed to read %s", POS_CONFIG_FILE_PATH);
        free(buf);
        return -4;
    }

    OSI_LOGI(TAG, "Loaded cached POS config from %s (%d bytes)", POS_CONFIG_FILE_PATH, flen);
    prv_parse_json_to_struct(buf, &g_pos_config);
    free(buf);

    if (g_pos_config.valid) {
        OSI_LOGI(TAG, "========================================");
        OSI_LOGI(TAG, "[POS_CONFIG] CACHE LOADED:");
        OSI_LOGI(TAG, "  MID     : %s", g_pos_config.mid);
        OSI_LOGI(TAG, "  TID     : %s", g_pos_config.tid);
        OSI_LOGI(TAG, "  Merchant: %s", g_pos_config.merchant_name);
        OSI_LOGI(TAG, "  VPA     : %s", g_pos_config.vpa);
        OSI_LOGI(TAG, "  City    : %s, %s", g_pos_config.merchant_city, g_pos_config.merchant_state);
        OSI_LOGI(TAG, "========================================");
        return 0;
    } else {
        OSI_LOGW(TAG, "Cached config invalid/corrupted, removing %s", POS_CONFIG_FILE_PATH);
        fs_remove(POS_CONFIG_FILE_PATH);
        return -5;
    }
}

static void prv_pos_config_http_callback(http_response_t *response, void *user_data)
{
    (void)user_data;
    s_fetch_in_progress = false;

    if (!response || response->result != HTTP_RESULT_SUCCESS || response->status_code != 200 || !response->response_data) {
        OSI_LOGE(TAG, "POS config fetch failed (code=%d, result=%d)",
                 response ? response->status_code : -1,
                 response ? response->result : -1);
        return;
    }

    OSI_LOGI(TAG, "POS config received (len=%u):", (unsigned int)response->response_len);
    OSI_LOG_LONG(TAG, "POS_CFG_RESP", response->response_data);

    customer_pos_config_t new_cfg = {0};
    prv_parse_json_to_struct(response->response_data, &new_cfg);

    if (new_cfg.valid) {
        memcpy(&g_pos_config, &new_cfg, sizeof(customer_pos_config_t));
        OSI_LOGI(TAG, "========================================");
        OSI_LOGI(TAG, "[POS_CONFIG] FETCH SUCCESS & ACTIVATED:");
        OSI_LOGI(TAG, "  MID     : %s", g_pos_config.mid);
        OSI_LOGI(TAG, "  TID     : %s", g_pos_config.tid);
        OSI_LOGI(TAG, "  Merchant: %s", g_pos_config.merchant_name);
        OSI_LOGI(TAG, "  VPA     : %s", g_pos_config.vpa);
        OSI_LOGI(TAG, "========================================");

        /* Save to flash file for subsequent boots */
        prv_save_config_to_file(response->response_data, response->response_len);
    } else {
        OSI_LOGE(TAG, "Failed to parse POS config from server response");
    }
}

int customer_pos_config_init(void)
{
    memset(&g_pos_config, 0, sizeof(g_pos_config));
    s_fetch_in_progress = false;
    return prv_load_config_from_file();
}

bool customer_pos_config_is_ready(void)
{
    return g_pos_config.valid;
}

int customer_pos_config_fetch_async(void)
{
    if (s_fetch_in_progress) {
        OSI_LOGW(TAG, "POS config fetch already in progress");
        return 0;
    }

    if (!framework_network_is_connected()) {
        OSI_LOGW(TAG, "Network not connected, cannot fetch POS config");
        return -1;
    }

    char sn[64] = {0};
    framework_device_config_get_sn(sn, sizeof(sn));
    if (strlen(sn) == 0) {
        const char *modem_imei = framework_network_get_imei();
        if (modem_imei && strlen(modem_imei) > 0) {
            snprintf(sn, sizeof(sn), "BP02%s", modem_imei);
        } else {
            strncpy(sn, TMS_HEARTBEAT_TID, sizeof(sn) - 1);
        }
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/%s", TMS_POS_CONFIG_URL_BASE, sn);
    static const char *header = "Authorization: " TMS_POS_CONFIG_AUTH_TOKEN "\r\n";

    OSI_LOGI(TAG, "Requesting POS config GET from: %s", url);
    s_fetch_in_progress = true;

    int ret = framework_http_get(url, header, prv_pos_config_http_callback, NULL);
    if (ret != 0) {
        OSI_LOGE(TAG, "Failed to queue POS config HTTP request: %d", ret);
        s_fetch_in_progress = false;
    }
    return ret;
}

const customer_pos_config_t* customer_pos_config_get(void)
{
    return g_pos_config.valid ? &g_pos_config : NULL;
}

const char* customer_pos_config_get_mid(void)
{
    if (g_pos_config.valid && strlen(g_pos_config.mid) > 0) {
        return g_pos_config.mid;
    }
    return "";
}

const char* customer_pos_config_get_tid(void)
{
    if (g_pos_config.valid && strlen(g_pos_config.tid) > 0) {
        return g_pos_config.tid;
    }
    return "";
}

const char* customer_pos_config_get_merchant_name(void)
{
    if (g_pos_config.valid && strlen(g_pos_config.merchant_name) > 0) {
        return g_pos_config.merchant_name;
    }
    return "";
}

const char* customer_pos_config_get_vpa(void)
{
    if (g_pos_config.valid && strlen(g_pos_config.vpa) > 0) {
        return g_pos_config.vpa;
    }
    return "";
}

const char* customer_pos_config_get_sn(void)
{
    if (g_pos_config.valid && strlen(g_pos_config.serial_number) > 0) {
        return g_pos_config.serial_number;
    }
    return "";
}

