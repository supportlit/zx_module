/**
 * @file customer_app_update.c
 * @brief Application & Firmware OTA update handler
 */

#include "customer_app_update.h"
#include "framework_http.h"
#include "framework_audio.h"
#include "framework_log.h"
#include "osi_api.h"
#include <string.h>
#include <stdio.h>

#define TAG "APP_UPDATE"

int customer_app_update_start(const char *url, const char *type)
{
    if (!url || url[0] == '\0') {
        OSI_LOGE(TAG, "[OTA-UPDATE] URL is empty or NULL");
        return -1;
    }

    OSI_LOGI(TAG, "[OTA-UPDATE] type:%s url=%s",
             type ? type : "(none)", url);

    if (type && strcmp(type, "wifi-firmware") == 0) {
        OSI_LOGI(TAG, "[OTA-UPDATE] Starting WiFi firmware download: %s", url);
        int ret = framework_http_download_file(url, "D:/wifi_firmware.bin",
                                               HTTP_DOWNLOAD_TYPE_WIFI_FW_OTA);
        if (ret == 0) {
            audio_resource_play_combo_async_mid_priority("", "upgrade-ing.mp3", NULL, 1);
        } else {
            OSI_LOGE(TAG, "[OTA-UPDATE] Failed to queue WiFi firmware download (err=%d)", ret);
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
        }
        return ret;
    }

    // Support both zip and raw bin download for App OTA
    const char *ext = strrchr(url, '.');
    bool is_zip_url = (ext && (strncasecmp(ext, ".zip", 4) == 0));
    const char *save_path = is_zip_url ? "D:/ota_update.zip" : "D:/ota_update.bin";

    OSI_LOGI(TAG, "[OTA-UPDATE] Starting App OTA download: url=%s save_path=%s", url, save_path);

    int ret = framework_http_download_file(url, save_path, HTTP_DOWNLOAD_TYPE_FW_OTA);
    if (ret == 0) {
        audio_resource_play_combo_async_mid_priority("", "upgrade-ing.mp3", NULL, 1);
    } else {
        OSI_LOGE(TAG, "[OTA-UPDATE] Failed to queue App OTA download (err=%d)", ret);
        audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
    }

    return ret;
}

int customer_app_update_from_cjson(const cJSON *root)
{
    if (!root) {
        OSI_LOGE(TAG, "[OTA-UPDATE] cJSON root is NULL");
        return -1;
    }

    const cJSON *j_url = cJSON_GetObjectItem(root, "url");
    const cJSON *j_type = cJSON_GetObjectItem(root, "type");
    const char *url = (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : "";
    const char *type = (j_type && cJSON_IsString(j_type)) ? j_type->valuestring : NULL;

    return customer_app_update_start(url, type);
}

int customer_app_update_from_json(const char *json_str)
{
    if (!json_str || json_str[0] == '\0') {
        OSI_LOGE(TAG, "[OTA-UPDATE] JSON string is empty or NULL");
        return -1;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        OSI_LOGE(TAG, "[OTA-UPDATE] Failed to parse JSON string: %s", json_str);
        return -2;
    }

    int ret = customer_app_update_from_cjson(root);
    cJSON_Delete(root);
    return ret;
}
