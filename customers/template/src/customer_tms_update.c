/**
 * @file customer_tms_update.c
 * @brief TMS (Terminal Management System) OTA & Audio Update Handler
 */

#include "customer_tms_update.h"
#include "customer_pos_config.h"
#include "customer_config.h"
#include "cJSON.h"
#include "framework_device_config.h"
#include "framework_network.h"
#include "framework_battery.h"
#include "framework_audio.h"
#include "framework_http.h"
#include "framework_unzip.h"
#include "framework_log.h"
#include "fs_api.h"
#include "fota_api.h"
#include "mbedtls/md5.h"
#include "osi_api.h"
#include "app_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define TAG "CUSTOMER_TMS"

#ifndef TMS_UPDATE_AUTH_TOKEN
#define TMS_UPDATE_AUTH_TOKEN "f1c3a7e8b9d5e2a0dh7d8f9dh97d"
#endif

#ifndef TMS_TEST_SERIAL_NUM
#define TMS_TEST_SERIAL_NUM   "1000000001"
#endif

#ifndef TMS_TEST_TERMINAL_ID
#define TMS_TEST_TERMINAL_ID  "11112222"
#endif

typedef enum {
    TMS_UPDATE_STATE_IDLE = 0,
    TMS_UPDATE_STATE_CHECK_VER,
    TMS_UPDATE_STATE_DOWNLOADING,
    TMS_UPDATE_STATE_UNZIPPING,
    TMS_UPDATE_STATE_REPORTING_RESULT
} tms_update_state_t;

#define TMS_MAX_PATH_LEN 128

typedef struct {
    tms_update_state_t state;
    bool is_audio;
    char sn[DEVICE_SN_MAX_LEN];
    char app_identifier[32];
    char app_target_ver[32];
    char download_url[HTTP_MAX_URL_LEN];
    char expected_md5[33];
    char save_path[TMS_MAX_PATH_LEN];
    uint32_t start_ticks;
    uint32_t duration_sec;
    bool reboot_pending;
    uint8_t check_ver_retries;
} tms_update_ctx_t;

static tms_update_ctx_t s_tms_ctx = {0};

static bool prv_file_exists(const char *path) {
    if (!path) return false;
    int fd = fs_open(path, "rb");
    if (fd >= 0) {
        fs_close(fd);
        return true;
    }
    return false;
}

/* Forward Declarations */
static void prv_check_ver_callback(http_response_t *response, void *user_data);
static void prv_download_callback(uint32_t msg_id, const char *save_path, void *user_data);
static void prv_send_up_result(const char *ret, const char *ret_desc);
static void prv_up_result_callback(http_response_t *response, void *user_data);

/* ============================================================================
 * Helper: Calculate File MD5
 * ============================================================================ */
static int prv_calc_file_md5(const char *path, char *md5_out33) {
    if (!path || !md5_out33) {
        return -1;
    }

    int fd = fs_open(path, "rb");
    if (fd < 0) {
        return -2;
    }

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);

    uint8_t buf[2048];
    int read_len = 0;
    while ((read_len = fs_read(buf, 1, sizeof(buf), fd)) > 0) {
        mbedtls_md5_update_ret(&ctx, buf, read_len);
    }
    fs_close(fd);

    uint8_t md5_raw[16];
    mbedtls_md5_finish_ret(&ctx, md5_raw);
    mbedtls_md5_free(&ctx);

    for (int i = 0; i < 16; ++i) {
        sprintf(md5_out33 + (i * 2), "%02x", md5_raw[i]);
    }
    md5_out33[32] = '\0';
    return 0;
}

/* ============================================================================
 * Step 1: Start TMS Update (Triggered by /tmsUpdate)
 * ============================================================================ */
int customer_tms_update_start(void) {
    if (s_tms_ctx.state != TMS_UPDATE_STATE_IDLE) {
        OSI_LOGW(TAG, "[TMS] Update already in progress (state: %d)", s_tms_ctx.state);
        return -1;
    }

    if (!framework_network_is_connected()) {
        OSI_LOGW(TAG, "[TMS] Network not connected, skipping update check");
        return -2;
    }

    uint8_t retries = s_tms_ctx.check_ver_retries;
    memset(&s_tms_ctx, 0, sizeof(s_tms_ctx));
    s_tms_ctx.check_ver_retries = retries;

    /* 1. Retrieve real device Serial Number */
    framework_device_config_get_sn(s_tms_ctx.sn, sizeof(s_tms_ctx.sn));
    const char *modem_imei = framework_network_get_imei();
    if (!modem_imei || strlen(modem_imei) == 0) {
        modem_imei = "000000000000000";
    }

    if (strlen(s_tms_ctx.sn) == 0) {
        if (strcmp(modem_imei, "000000000000000") != 0) {
            snprintf(s_tms_ctx.sn, sizeof(s_tms_ctx.sn), "BP02%s", modem_imei);
        } else {
            strncpy(s_tms_ctx.sn, TMS_TEST_SERIAL_NUM, sizeof(s_tms_ctx.sn) - 1);
        }
    }

    /* 2. Retrieve real charging status ("Y" or "N") */
    const char *charging = framework_battery_is_charging() ? "Y" : "N";

    /* 3. Retrieve real bearer type ("W" for WiFi, "G" for 4G/LTE) */
    const char *comm_type = framework_network_wifi_is_connected() ? "W" : "G";

    /* 4. Retrieve real local/NTP timestamp (YYYYMMDDHHMMSS) */
    char term_time[32] = {0};
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info && tm_info->tm_year > 120) {
        snprintf(term_time, sizeof(term_time), "%04d%02d%02d%02d%02d%02d",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else {
        strncpy(term_time, "20261027120000", sizeof(term_time) - 1);
    }

    /* 5. Retrieve real Cell Tower LAC as latlng if available */
    char latlng[32] = {0};
    int lac = framework_network_get_lac();
    if (lac > 0) {
        snprintf(latlng, sizeof(latlng), "%d", lac);
    }

    /* 6. Real versions & config */
    const char *fw_ver = TMS_HEARTBEAT_FIRMWARE_VER;
    const char *mid = customer_pos_config_get_mid();
    if (!mid || strlen(mid) == 0) {
        mid = TMS_HEARTBEAT_MID;
    }
    const char *tid = customer_pos_config_get_tid();
    if (!tid || strlen(tid) == 0) {
        tid = TMS_HEARTBEAT_TID;
    }

    /* Allocate JSON body on heap to avoid stack overflow */
    char *json_body = (char *)malloc(1024);
    if (!json_body) {
        OSI_LOGE(TAG, "[TMS] Failed to allocate checkVer json body");
        return -3;
    }

    int json_len = snprintf(json_body, 1024,
        "{\n"
        "    \"apkInfo\": [\n"
        "        {\n"
        "            \"appIdentifier\": \"NA-App\",\n"
        "            \"appVersion\": \"%s\"\n"
        "        }\n"
        "    ],\n"
        "    \"configInfo\": {\n"
        "        \"configVersion\": \"%s\",\n"
        "        \"masterVersion\": \"10\"\n"
        "    },\n"
        "    \"forceParamCheck\": true,\n"
        "    \"heartbeatInfo\": {\n"
        "        \"charging\": \"%s\",\n"
        "        \"commType\": \"%s\",\n"
        "        \"imei\": \"%s\",\n"
        "        \"latlng\": \"%s\",\n"
        "        \"termTime\": \"%s\"\n"
        "    },\n"
        "    \"terminalInfo\": {\n"
        "        \"firmwareVersion\": \"%s\",\n"
        "        \"merchantId\": \"%s\",\n"
        "        \"paymentAppVersion\": \"%s\",\n"
        "        \"serialNum\": \"%s\",\n"
        "        \"terminalId\": \"%s\"\n"
        "    }\n"
        "}",
        fw_ver,
        TMS_HEARTBEAT_CONFIG_VER,
        charging,
        comm_type,
        modem_imei,
        latlng,
        term_time,
        fw_ver,
        mid,
        fw_ver,
        s_tms_ctx.sn,
        tid
    );

    if (json_len < 0 || json_len >= 1024) {
        OSI_LOGE(TAG, "[TMS] checkVer JSON buffer overflow");
        free(json_body);
        return -4;
    }

    char url[256];
    snprintf(url, sizeof(url), "https://103.157.22.211:8075/sxtms/online/tms/%s/checkVer", s_tms_ctx.sn);
    static const char *header = "Authorization: " TMS_UPDATE_AUTH_TOKEN "\r\nContent-Type: application/json\r\n";

    OSI_LOGI(TAG, "[TMS] Requesting checkVer from %s:", url);
    OSI_LOG_LONG(TAG, "CHECKVER_REQ", json_body);
    s_tms_ctx.state = TMS_UPDATE_STATE_CHECK_VER;

    int ret = framework_http_post_json(url, json_body, header, prv_check_ver_callback, NULL);
    free(json_body);

    if (ret != 0) {
        OSI_LOGE(TAG, "[TMS] Failed to queue checkVer request (%d)", ret);
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
    }
    return ret;
}

/* ============================================================================
 * Step 2: Handle checkVer Response
 * ============================================================================ */
static void prv_check_ver_callback(http_response_t *response, void *user_data) {
    (void)user_data;
    if (!response || response->result != HTTP_RESULT_SUCCESS || response->status_code != 200 || !response->response_data) {
        OSI_LOGE(TAG, "[TMS] checkVer HTTP request failed (code=%d, result=%d)",
                 response ? response->status_code : -1, response ? response->result : -1);
        if (s_tms_ctx.check_ver_retries < 2) {
            s_tms_ctx.check_ver_retries++;
            OSI_LOGI(TAG, "[TMS] Retrying checkVer in 2s (attempt %d/2)...", s_tms_ctx.check_ver_retries);
            osiThreadSleep(2000);
            s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
            customer_tms_update_start();
            return;
        }
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        return;
    }

    OSI_LOGI(TAG, "[TMS] checkVer response received (len=%d):", (int)response->response_len);
    OSI_LOG_LONG(TAG, "RAW", response->response_data);

    cJSON *root = cJSON_Parse(response->response_data);
    if (!root) {
        OSI_LOGE(TAG, "[TMS] JSON parse error on checkVer response");
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        return;
    }

    const cJSON *j_ret = cJSON_GetObjectItem(root, "retCode");
    if (!j_ret || !cJSON_IsString(j_ret) || strcmp(j_ret->valuestring, "SUCC") != 0) {
        OSI_LOGW(TAG, "[TMS] checkVer retCode is not SUCC (%s)", j_ret ? j_ret->valuestring : "null");
        cJSON_Delete(root);
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        return;
    }

    const cJSON *j_cfg = cJSON_GetObjectItem(root, "cfgDetails");
    if (j_cfg) {
        const cJSON *j_mid = cJSON_GetObjectItem(j_cfg, "mid");
        const cJSON *j_tid = cJSON_GetObjectItem(j_cfg, "tid");
        const cJSON *j_mname = cJSON_GetObjectItem(j_cfg, "merchantName");
        OSI_LOGI(TAG, "[TMS] Config Details: MID=%s, TID=%s, Merchant='%s'",
                 (j_mid && cJSON_IsString(j_mid)) ? j_mid->valuestring : "",
                 (j_tid && cJSON_IsString(j_tid)) ? j_tid->valuestring : "",
                 (j_mname && cJSON_IsString(j_mname)) ? j_mname->valuestring : "");
    }

    const cJSON *j_apps = cJSON_GetObjectItem(root, "appInfos");
    if (!j_apps || !cJSON_IsArray(j_apps) || cJSON_GetArraySize(j_apps) == 0) {
        OSI_LOGI(TAG, "[TMS] No appInfos array or empty, no update available");
        cJSON_Delete(root);
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        return;
    }

    bool update_found = false;
    int size = cJSON_GetArraySize(j_apps);
    for (int i = 0; i < size; i++) {
        cJSON *item = cJSON_GetArrayItem(j_apps, i);
        if (!item) continue;

        cJSON *j_flag = cJSON_GetObjectItem(item, "appUpFlag");
        if (j_flag && cJSON_IsString(j_flag) && strcmp(j_flag->valuestring, "Y") == 0) {
            cJSON *j_url = cJSON_GetObjectItem(item, "appDownURL");
            cJSON *j_id = cJSON_GetObjectItem(item, "appIdentifier");
            cJSON *j_ver = cJSON_GetObjectItem(item, "appNewVer");
            cJSON *j_md5 = cJSON_GetObjectItem(item, "paramMd5");

            if (j_url && cJSON_IsString(j_url) && strlen(j_url->valuestring) > 0) {
                strncpy(s_tms_ctx.download_url, j_url->valuestring, sizeof(s_tms_ctx.download_url) - 1);
                strncpy(s_tms_ctx.app_identifier, (j_id && cJSON_IsString(j_id)) ? j_id->valuestring : "NA-App", sizeof(s_tms_ctx.app_identifier) - 1);
                strncpy(s_tms_ctx.app_target_ver, (j_ver && cJSON_IsString(j_ver)) ? j_ver->valuestring : "1.0.0", sizeof(s_tms_ctx.app_target_ver) - 1);

                if (j_md5 && cJSON_IsString(j_md5) && strlen(j_md5->valuestring) > 0) {
                    strncpy(s_tms_ctx.expected_md5, j_md5->valuestring, sizeof(s_tms_ctx.expected_md5) - 1);
                } else {
                    s_tms_ctx.expected_md5[0] = '\0';
                }

                update_found = true;
                break;
            }
        }
    }

    cJSON_Delete(root);

    if (!update_found) {
        OSI_LOGI(TAG, "[TMS] No update flagged with appUpFlag='Y'");
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        return;
    }

    /* Check if target is an Audio Resource (.zip or AUDIO/SOUND) or Firmware Binary */
    s_tms_ctx.is_audio = (strstr(s_tms_ctx.download_url, ".zip") != NULL ||
                          strcasecmp(s_tms_ctx.app_identifier, "AUDIO") == 0 ||
                          strcasecmp(s_tms_ctx.app_identifier, "SOUND") == 0 ||
                          strcasecmp(s_tms_ctx.app_identifier, "RES") == 0);

    const char *save_path = s_tms_ctx.is_audio ? "D:/template_sound_update.zip" : "D:/customer_app_update.bin";
    strncpy(s_tms_ctx.save_path, save_path, sizeof(s_tms_ctx.save_path) - 1);

    /* Delete old residual file to clean space */
    fs_remove(save_path);

    OSI_LOGI(TAG, "[TMS] ========================================");
    OSI_LOGI(TAG, "[TMS] UPDATE TARGET FOUND:");
    OSI_LOGI(TAG, "[TMS]   App ID  : %s", s_tms_ctx.app_identifier);
    OSI_LOGI(TAG, "[TMS]   Target  : %s", s_tms_ctx.app_target_ver);
    OSI_LOGI(TAG, "[TMS]   Type    : %s", s_tms_ctx.is_audio ? "AUDIO (.zip)" : "FIRMWARE (.bin)");
    OSI_LOGI(TAG, "[TMS]   SavePath: %s", s_tms_ctx.save_path);
    OSI_LOGI(TAG, "[TMS]   Exp MD5 : %s", s_tms_ctx.expected_md5[0] ? s_tms_ctx.expected_md5 : "(none)");
    OSI_LOGI(TAG, "[TMS]   Down URL: %s", s_tms_ctx.download_url);
    OSI_LOGI(TAG, "[TMS] ========================================");

    s_tms_ctx.start_ticks = osiGetTicks();
    s_tms_ctx.state = TMS_UPDATE_STATE_DOWNLOADING;
    s_tms_ctx.reboot_pending = false;

    if (prv_file_exists("D:/FEMALE/upgrade-ing.mp3")) {
        audio_resource_play_combo_async_mid_priority("", "upgrade-ing.mp3", NULL, 1);
    }

    static const char *download_auth_header = "Authorization: " TMS_UPDATE_AUTH_TOKEN "\r\n";
    int ret = framework_http_download_file_callback_with_header(
        s_tms_ctx.download_url,
        s_tms_ctx.save_path,
        HTTP_DOWNLOAD_TYPE_FILE,
        download_auth_header,
        s_tms_ctx.expected_md5,
        prv_download_callback,
        NULL
    );

    if (ret != 0) {
        OSI_LOGE(TAG, "[TMS] Failed to start download (%d)", ret);
        prv_send_up_result("FAIL", "DOWNLOAD_START_FAILED");
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
    }
}

/* ============================================================================
 * Step 3: Handle Download Completion & Integrity Verification
 * ============================================================================ */
static void prv_download_callback(uint32_t msg_id, const char *save_path, void *user_data) {
    (void)user_data;
    uint32_t elapsed = (osiGetTicks() - s_tms_ctx.start_ticks) / 1000;
    s_tms_ctx.duration_sec = (elapsed == 0) ? 1 : elapsed;

    if (msg_id != MSG_HTTP_DOWNLOAD_SUCCESS) {
        OSI_LOGE(TAG, "[TMS] Download failed (msg_id=%d, path=%s)", msg_id, save_path ? save_path : "");
        if (prv_file_exists("D:/FEMALE/upgrade-failed.mp3")) {
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
        } else {
            audio_resource_play_combo_async_mid_priority("", "server-failed.mp3", NULL, 1);
        }
        prv_send_up_result("FAIL", "DOWNLOAD_FAILED");
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        return;
    }

    OSI_LOGI(TAG, "[TMS] Download SUCCESS (%s), duration=%u s", save_path, (unsigned int)s_tms_ctx.duration_sec);

    /* Verify MD5 if provided */
    if (s_tms_ctx.expected_md5[0] != '\0') {
        char actual_md5[33] = {0};
        if (prv_calc_file_md5(save_path, actual_md5) != 0 ||
            strcasecmp(actual_md5, s_tms_ctx.expected_md5) != 0) {
            OSI_LOGE(TAG, "[TMS] MD5 MISMATCH! Expected=%s, Got=%s", s_tms_ctx.expected_md5, actual_md5);
            if (prv_file_exists("D:/FEMALE/upgrade-failed.mp3")) {
                audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            } else {
                audio_resource_play_combo_async_mid_priority("", "server-failed.mp3", NULL, 1);
            }
            prv_send_up_result("FAIL", "MD5_MISMATCH");
            fs_remove(save_path);
            s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
            return;
        }
        OSI_LOGI(TAG, "[TMS] MD5 MATCHED: %s", actual_md5);
    } else {
        OSI_LOGI(TAG, "[TMS] No MD5 verification required");
    }

    if (s_tms_ctx.is_audio) {
        /* Audio Update: Unpack zip file into D:/ */
        OSI_LOGI(TAG, "[TMS] Starting unzip of audio pack to D:/...");
        s_tms_ctx.state = TMS_UPDATE_STATE_UNZIPPING;
        int unzip_ret = framework_unzip_start(save_path, "D:/", FRAMEWORK_UNZIP_TYPE_RESOURCES);
        if (unzip_ret != 0) {
            OSI_LOGE(TAG, "[TMS] Failed to start unzip (%d)", unzip_ret);
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            prv_send_up_result("FAIL", "UNZIP_START_FAILED");
            fs_remove(save_path);
            s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        }
    } else {
        /* Firmware Update: Validate file existence and non-zero size */
        int test_fd = fs_open(save_path, "rb");
        if (test_fd < 0) {
            OSI_LOGE(TAG, "[TMS] Firmware image file cannot be opened!");
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            prv_send_up_result("FAIL", "FILE_OPEN_FAILED");
            fs_remove(save_path);
            s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
            return;
        }
        int file_len = fs_seek(test_fd, 0, 2 /* SEEK_END */);
        fs_close(test_fd);
        if (file_len <= 0) {
            OSI_LOGE(TAG, "[TMS] Firmware image file is empty!");
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            prv_send_up_result("FAIL", "EMPTY_FILE");
            fs_remove(save_path);
            s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
            return;
        }

        OSI_LOGI(TAG, "[TMS] Firmware file valid (size: %d bytes). Reporting SUCCESS to TMS before reboot...", file_len);
        s_tms_ctx.reboot_pending = true;
        prv_send_up_result("SUCC", "SUCCESS");
    }
}

/* ============================================================================
 * Step 4: Handle Audio Unzip Completion
 * ============================================================================ */
void customer_tms_update_handle_unzip_msg(framework_msg_t *msg) {
    if (!msg || s_tms_ctx.state != TMS_UPDATE_STATE_UNZIPPING) {
        return;
    }

    framework_unzip_type_t type = (framework_unzip_type_t)msg->param1;
    if (type != FRAMEWORK_UNZIP_TYPE_RESOURCES) {
        return;
    }

    if (msg->id == MSG_UNZIP_SUCCESS) {
        OSI_LOGI(TAG, "[TMS] Audio unzip SUCCESSFUL!");
        fs_remove(s_tms_ctx.save_path); /* Clean up zip file */
        audio_resource_play_combo_async_mid_priority("", "upgrade-success.mp3", NULL, 1);
        prv_send_up_result("SUCC", "SUCCESS");
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
    } else if (msg->id == MSG_UNZIP_FAIL) {
        OSI_LOGE(TAG, "[TMS] Audio unzip FAILED!");
        fs_remove(s_tms_ctx.save_path);
        audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
        prv_send_up_result("FAIL", "UNZIP_FAILED");
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
    }
}

/* ============================================================================
 * Step 5: Send upResult to TMS Server
 * ============================================================================ */
static void prv_send_up_result(const char *ret, const char *ret_desc) {
    char url[256];
    snprintf(url, sizeof(url), "https://103.157.22.211:8075/sxtms/online/tms/%s/upResult", s_tms_ctx.sn);

    char timestamp[32] = {0};
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info && tm_info->tm_year > 120) {
        snprintf(timestamp, sizeof(timestamp), "%04d%02d%02d%02d%02d%02d",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else {
        strncpy(timestamp, "20240205093821", sizeof(timestamp) - 1);
    }

    const char *comm_type = framework_network_wifi_is_connected() ? "W" : "G";

    char *json_body = (char *)malloc(1024);
    if (!json_body) {
        OSI_LOGE(TAG, "[TMS] Failed to allocate upResult json body");
        if (s_tms_ctx.reboot_pending) {
            fupdateAppSetReady(s_tms_ctx.save_path);
            osiThreadSleep(1000);
            osiSysReset();
        }
        return;
    }

    snprintf(json_body, 1024,
        "{\n"
        "    \"headMsg\": {\n"
        "        \"oid\": \"10004002\",\n"
        "        \"firmCode\": \"NEWLAND\",\n"
        "        \"productType\": \"N910\",\n"
        "        \"productType2\": \"N910\",\n"
        "        \"timestamp\": \"%s\",\n"
        "        \"version\": \"3.3.07.35\",\n"
        "        \"posVerType\": \"Newland\"\n"
        "    },\n"
        "    \"bodyMsg\": {\n"
        "        \"upCommType\": \"%s\",\n"
        "        \"appInfos\": [\n"
        "            {\n"
        "                \"upAppIdentifier\": \"%s\",\n"
        "                \"appURL\": \"%s\",\n"
        "                \"upVer\": \"%s\",\n"
        "                \"upRet\": \"%s\",\n"
        "                \"upRetDesc\": \"%s\",\n"
        "                \"upDuration\": \"%u\"\n"
        "            }\n"
        "        ]\n"
        "    }\n"
        "}",
        timestamp,
        comm_type,
        s_tms_ctx.app_identifier,
        s_tms_ctx.download_url,
        s_tms_ctx.app_target_ver,
        ret,
        ret_desc,
        (unsigned int)s_tms_ctx.duration_sec
    );

    static const char *header = "Authorization: " TMS_UPDATE_AUTH_TOKEN "\r\nContent-Type: application/json\r\n";
    OSI_LOGI(TAG, "[TMS] Sending upResult to %s (ret=%s, desc=%s):", url, ret, ret_desc);
    OSI_LOG_LONG(TAG, "UP_RESULT_REQ", json_body);

    s_tms_ctx.state = TMS_UPDATE_STATE_REPORTING_RESULT;
    framework_http_post_json(url, json_body, header, prv_up_result_callback, NULL);
    free(json_body);
}

/* ============================================================================
 * Step 6: upResult Callback & Firmware Reboot Execution
 * ============================================================================ */
static void prv_up_result_callback(http_response_t *response, void *user_data) {
    (void)user_data;
    if (response) {
        OSI_LOGI(TAG, "[TMS] upResult response [HTTP %d, len=%u]:",
                 response->status_code, (unsigned int)response->response_len);
        if (response->response_data) {
            OSI_LOG_LONG(TAG, "UP_RESULT", response->response_data);
        }
    }

    if (s_tms_ctx.reboot_pending) {
        OSI_LOGI(TAG, "[TMS] Setting firmware ready for bootloader: %s", s_tms_ctx.save_path);
        if (fupdateAppSetReady(s_tms_ctx.save_path)) {
            OSI_LOGI(TAG, "[TMS] fupdateAppSetReady SUCCESS! Rebooting in 1000ms...");
            if (prv_file_exists("D:/FEMALE/upgrade-success.mp3")) {
                audio_resource_play_combo_async_mid_priority("", "upgrade-success.mp3", NULL, 1);
            } else {
                audio_resource_play_combo_async_mid_priority("", "server-success.mp3", NULL, 1);
            }
            osiThreadSleep(1000);
            osiSysReset();
        } else {
            OSI_LOGE(TAG, "[TMS] fupdateAppSetReady FAILED!");
            if (prv_file_exists("D:/FEMALE/upgrade-failed.mp3")) {
                audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            } else {
                audio_resource_play_combo_async_mid_priority("", "server-failed.mp3", NULL, 1);
            }
            s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
        }
    } else {
        s_tms_ctx.state = TMS_UPDATE_STATE_IDLE;
    }
}
