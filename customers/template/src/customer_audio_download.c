/**
 * @file customer_audio_download.c
 * @brief Audio file and resource pack download handler
 */

#include "customer_audio_download.h"
#include "customer_mqtt_handler.h"
#include "customer_status_report.h"
#include "framework_http.h"
#include "framework_audio.h"
#include "framework_log.h"
#include "framework_unzip.h"
#include "fs_api.h"
#include "osi_api.h"
#include <stdio.h>
#include <string.h>

#define TAG "AUDIO_DL"

static char s_pending_audio_download_lang[16] = {0};

static void customer_audio_download_callback(uint32_t msg_id, const char *save_path, void *user_data)
{
    (void)user_data;

    if (msg_id == MSG_HTTP_DOWNLOAD_START) {
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download started: %s", save_path ? save_path : "");
    } else if (msg_id == MSG_HTTP_DOWNLOAD_PROGRESS) {
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download in progress: %s", save_path ? save_path : "");
    } else if (msg_id == MSG_HTTP_DOWNLOAD_SUCCESS) {
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download SUCCESS: %s", save_path ? save_path : "");
        if (save_path && (strstr(save_path, ".zip") || strstr(save_path, ".ZIP"))) {
            OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Zip file detected: %s. Starting unzip to D:/FEMALE/...", save_path);
            int uz_ret = framework_unzip_start(save_path, "D:/FEMALE/", FRAMEWORK_UNZIP_TYPE_RESOURCES);
            if (uz_ret != 0) {
                OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] Failed to start unzip (%d)", uz_ret);
            }
            return;
        }

        if (save_path && (strstr(save_path, "NetConfig") || strstr(save_path, ".txt") || strstr(save_path, ".TXT"))) {
            OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] File downloaded successfully to %s (no audio playback/reset needed)", save_path);
            return;
        }

        if (s_pending_audio_download_lang[0] != '\0') {
            customer_set_default_language(s_pending_audio_download_lang);
            audio_resource_set_language_prefix("D:/FEMALE/");
            OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download successful! Updated language to: %s", s_pending_audio_download_lang);
            s_pending_audio_download_lang[0] = '\0';
        }

        if (save_path && save_path[0] != '\0') {
            // Play the newly downloaded MP3 file
            audio_resource_play_combo_async_mid_priority("", save_path, NULL, 1);
        }
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Restarting device in 3 seconds...");
        osiThreadSleep(3000); // Allow audio to play before reset
        customer_dux_publish_device_power(0);
        osiThreadSleep(500);
        osiSysReset();
    } else if (msg_id == MSG_HTTP_DOWNLOAD_FAIL) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] Download FAILED for: %s", save_path ? save_path : "");
        s_pending_audio_download_lang[0] = '\0';
    }
}

int customer_audio_download_handle_zip(const char *url, const char *dst, const char *lang)
{
    if (!url || url[0] == '\0') {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD-ZIP] URL is empty");
        return -1;
    }

    const char *target_dst = (dst && dst[0] != '\0' && !strstr(dst, ".zip") && !strstr(dst, ".ZIP"))
                             ? dst : "D:/FEMALE/";
    OSI_LOGI(TAG, "[AUDIO-DOWNLOAD-ZIP] Zip package detected, delegating to sound update: url=%s dst=%s lang=%s",
             url, target_dst, lang ? lang : "(none)");

    int ret = template_start_sound_update(url, NULL, target_dst, lang, false, 0, NULL, NULL);
    if (ret != 0) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD-ZIP] Failed to start sound update (%d)", ret);
    }
    return ret;
}

int customer_audio_download_handle_single_mp3(const char *url, const char *dst, const char *lang)
{
    if (!url || url[0] == '\0') {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD-MP3] URL is empty");
        return -1;
    }

    // Single file download (e.g. mp3) - defer language update until download finishes
    if (lang && lang[0] != '\0') {
        strncpy(s_pending_audio_download_lang, lang, sizeof(s_pending_audio_download_lang) - 1);
        s_pending_audio_download_lang[sizeof(s_pending_audio_download_lang) - 1] = '\0';
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD-MP3] Pending language: %s (will apply after successful download)",
                 s_pending_audio_download_lang);
    } else {
        s_pending_audio_download_lang[0] = '\0';
    }

    char final_dst[128] = {0};
    if (dst && dst[0] != '\0') {
        if (dst[0] == 'D' && dst[1] == ':' && dst[2] == '/') {
            strncpy(final_dst, dst, sizeof(final_dst) - 1);
        } else {
            snprintf(final_dst, sizeof(final_dst), "D:/%s",
                     (dst[0] == '/') ? dst + 1 : dst);
        }
    } else {
        const char *slash = strrchr(url, '/');
        const char *fname = (slash && *(slash + 1)) ? (slash + 1) : "downloaded_audio.mp3";
        char clean_name[64] = {0};
        strncpy(clean_name, fname, sizeof(clean_name) - 1);
        char *qmark = strchr(clean_name, '?');
        if (qmark) {
            *qmark = '\0';
        }
        snprintf(final_dst, sizeof(final_dst), "D:/%s",
                 clean_name[0] ? clean_name : "downloaded_audio.mp3");
    }

    // Ensure parent directory exists if a subfolder is specified (e.g. D:/FEMALE/)
    char *last_slash = strrchr(final_dst, '/');
    if (last_slash && last_slash != final_dst && last_slash != (final_dst + 2)) {
        char dir_buf[128] = {0};
        size_t dir_len = (size_t)(last_slash - final_dst);
        if (dir_len < sizeof(dir_buf)) {
            strncpy(dir_buf, final_dst, dir_len);
            fs_makedir(dir_buf);
        }
    }

    OSI_LOGI(TAG, "[AUDIO-DOWNLOAD-MP3] Queuing HTTP download: %s -> %s", url, final_dst);

    int ret = framework_http_download_file_callback(
        url, final_dst, HTTP_DOWNLOAD_TYPE_FILE,
        customer_audio_download_callback, NULL);
    if (ret != 0) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD-MP3] Failed to queue download (%d)", ret);
    } else {
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD-MP3] Download queued successfully: %s -> %s",
                 url, final_dst);
    }

    return ret;
}

int customer_audio_download_start(const char *url, const char *dst,
                                  const char *lang, bool should_download)
{
    OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] download=%d, url=%s, dst=%s, lang=%s",
             should_download, url ? url : "(null)", dst ? dst : "(none)", lang ? lang : "(none)");

    if (!should_download || !url || url[0] == '\0') {
        OSI_LOGW(TAG, "[AUDIO-DOWNLOAD] Ignored: download flag false or url empty");
        return -1;
    }

    bool is_zip = false;
    if (strstr(url, ".zip") || strstr(url, ".ZIP")) {
        is_zip = true;
    }
    if (dst && (strstr(dst, ".zip") || strstr(dst, ".ZIP"))) {
        is_zip = true;
    }

    if (is_zip) {
        return customer_audio_download_handle_zip(url, dst, lang);
    } else {
        return customer_audio_download_handle_single_mp3(url, dst, lang);
    }
}

int customer_audio_download_from_json(const cJSON *root)
{
    if (!root) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] JSON root is NULL");
        return -1;
    }

    const cJSON *j_dl = cJSON_GetObjectItem(root, "download");
    const cJSON *j_url = cJSON_GetObjectItem(root, "url");
    const cJSON *j_dst = cJSON_GetObjectItem(root, "dst");
    const cJSON *j_lang = cJSON_GetObjectItem(root, "language");
    if (!j_lang) j_lang = cJSON_GetObjectItem(root, "Language");
    if (!j_lang) j_lang = cJSON_GetObjectItem(root, "lang");
    if (!j_lang) j_lang = cJSON_GetObjectItem(root, "Lang");

    const char *lang_str = (j_lang && cJSON_IsString(j_lang) && j_lang->valuestring && j_lang->valuestring[0] != '\0')
                           ? j_lang->valuestring : NULL;
    const char *url = (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : "";
    const char *dst = (j_dst && cJSON_IsString(j_dst)) ? j_dst->valuestring : "";

    bool should_download = false;
    if (j_dl) {
        if (cJSON_IsBool(j_dl)) {
            should_download = (j_dl->type == cJSON_True);
        } else if (cJSON_IsString(j_dl) && j_dl->valuestring) {
            if (strcmp(j_dl->valuestring, "true") == 0 ||
                strcmp(j_dl->valuestring, "TRUE") == 0 ||
                strcmp(j_dl->valuestring, "1") == 0) {
                should_download = true;
            }
        }
    } else {
        if (url && url[0] != '\0') {
            should_download = true;
        }
    }

    return customer_audio_download_start(url, dst, lang_str, should_download);
}

int customer_audio_download_from_json_str(const char *json_str)
{
    if (!json_str || json_str[0] == '\0') {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] JSON string is empty or NULL");
        return -1;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] Failed to parse JSON string: %s", json_str);
        return -2;
    }

    int ret = customer_audio_download_from_json(root);
    cJSON_Delete(root);
    return ret;
}
