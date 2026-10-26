/**
 * @file customer_audio_download.c
 * @brief Audio file and resource pack download handler
 */

#include "customer_audio_download.h"
#include "soundbox_language.h"
#include "customer_mqtt_handler.h"
#include "customer_status_report.h"
#include "framework_http.h"
#include "framework_audio.h"
#include "framework_log.h"
#include "framework_unzip.h"
#include "fs_api.h"
#include "osi_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define TAG "AUDIO_DL"
#define BATCH_MAX_FILES 64

typedef enum {
    BATCH_FILE_STATUS_IDLE = 0,
    BATCH_FILE_STATUS_QUEUED,
    BATCH_FILE_STATUS_SUCCESS,
    BATCH_FILE_STATUS_FAILED
} batch_file_status_t;

typedef struct {
    osiMutex_t *mutex;
    uint32_t batch_id;
    int total_required;
    int scheduled_count;
    int success_count;
    int failed_count;
    bool all_scheduled;
    bool completed;
    bool restart_requested;
    uint8_t file_status[BATCH_MAX_FILES];
} audio_batch_tracker_t;

static audio_batch_tracker_t g_batch_tracker = {0};
static uint32_t s_batch_counter = 0;

typedef struct {
    bool restart_after_download;
    uint32_t batch_id;
    int file_index;
    char lang[16];
} audio_download_ctx_t;

static void prv_batch_lock(void)
{
    if (!g_batch_tracker.mutex) {
        g_batch_tracker.mutex = osiMutexCreate();
    }
    if (g_batch_tracker.mutex) {
        osiMutexLock(g_batch_tracker.mutex);
    }
}

static void prv_batch_unlock(void)
{
    if (g_batch_tracker.mutex) {
        osiMutexUnlock(g_batch_tracker.mutex);
    }
}

void customer_audio_download_init(void)
{
    prv_batch_lock();
    if (!g_batch_tracker.mutex) {
        g_batch_tracker.mutex = osiMutexCreate();
    }
    prv_batch_unlock();
}

static void prv_delayed_restart_task(void *param)
{
    (void)param;
    OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Dedicated restart task sleeping 3000 ms before device reset...");
    osiThreadSleep(3000);
    customer_dux_publish_device_power(0);
    osiThreadSleep(500);
    osiSysReset();
    osiThreadExit();
}

static void prv_trigger_restart(void)
{
    OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Spawning dedicated restart task...");
    osiThread_t *t = osiThreadCreate("audio_rst", prv_delayed_restart_task, NULL, 120, 2048);
    if (!t) {
        OSI_LOGW(TAG, "[AUDIO-DOWNLOAD] Failed to create restart thread, executing inline restart sequence");
        osiThreadSleep(3000);
        customer_dux_publish_device_power(0);
        osiThreadSleep(500);
        osiSysReset();
    }
}

static void customer_audio_batch_check_completion_locked(void)
{
    if (g_batch_tracker.batch_id == 0) {
        return;
    }
    if (!g_batch_tracker.all_scheduled) {
        return;
    }
    if (g_batch_tracker.completed) {
        return;
    }

    int terminal_count = g_batch_tracker.success_count + g_batch_tracker.failed_count;
    if (terminal_count < g_batch_tracker.total_required) {
        return;
    }

    g_batch_tracker.completed = true;
    OSI_LOGI(TAG, "[BATCH #%u] Batch completed: total=%d, success=%d, failed=%d",
             g_batch_tracker.batch_id, g_batch_tracker.total_required,
             g_batch_tracker.success_count, g_batch_tracker.failed_count);

    if (g_batch_tracker.total_required > 0 &&
        g_batch_tracker.failed_count == 0 &&
        g_batch_tracker.success_count == g_batch_tracker.total_required &&
        !g_batch_tracker.restart_requested)
    {
        g_batch_tracker.restart_requested = true;
        OSI_LOGI(TAG, "[BATCH #%u] All %d required exception files downloaded successfully! Triggering single device restart.",
                 g_batch_tracker.batch_id, g_batch_tracker.total_required);
        prv_trigger_restart();
    } else {
        OSI_LOGW(TAG, "[BATCH #%u] Batch completed with errors or empty (total=%d, success=%d, failed=%d). No restart triggered.",
                 g_batch_tracker.batch_id, g_batch_tracker.total_required,
                 g_batch_tracker.success_count, g_batch_tracker.failed_count);
    }
}

static void customer_audio_batch_record_result(uint32_t batch_id, int file_idx, bool success)
{
    prv_batch_lock();
    if (g_batch_tracker.batch_id != batch_id || batch_id == 0) {
        prv_batch_unlock();
        return;
    }

    if (file_idx >= 0 && file_idx < BATCH_MAX_FILES) {
        batch_file_status_t prev_status = (batch_file_status_t)g_batch_tracker.file_status[file_idx];
        if (success) {
            if (prev_status == BATCH_FILE_STATUS_SUCCESS) {
                OSI_LOGW(TAG, "[BATCH #%u] Duplicate success callback for file #%d, ignoring",
                         batch_id, file_idx);
                prv_batch_unlock();
                return;
            }
            if (prev_status == BATCH_FILE_STATUS_FAILED) {
                g_batch_tracker.failed_count--;
            }
            g_batch_tracker.file_status[file_idx] = BATCH_FILE_STATUS_SUCCESS;
            g_batch_tracker.success_count++;
            OSI_LOGI(TAG, "[BATCH #%u] File #%d download SUCCESS (success=%d, failed=%d, total=%d)",
                     batch_id, file_idx, g_batch_tracker.success_count,
                     g_batch_tracker.failed_count, g_batch_tracker.total_required);
        } else {
            if (prev_status == BATCH_FILE_STATUS_FAILED) {
                OSI_LOGW(TAG, "[BATCH #%u] Duplicate failure callback for file #%d, ignoring",
                         batch_id, file_idx);
                prv_batch_unlock();
                return;
            }
            if (prev_status == BATCH_FILE_STATUS_SUCCESS) {
                g_batch_tracker.success_count--;
            }
            g_batch_tracker.file_status[file_idx] = BATCH_FILE_STATUS_FAILED;
            g_batch_tracker.failed_count++;
            OSI_LOGE(TAG, "[BATCH #%u] File #%d download FAILED (success=%d, failed=%d, total=%d)",
                     batch_id, file_idx, g_batch_tracker.success_count,
                     g_batch_tracker.failed_count, g_batch_tracker.total_required);
        }
    } else {
        if (success) {
            g_batch_tracker.success_count++;
        } else {
            g_batch_tracker.failed_count++;
        }
    }

    customer_audio_batch_check_completion_locked();
    prv_batch_unlock();
}

uint32_t customer_audio_batch_start(int total_required)
{
    if (total_required <= 0) {
        OSI_LOGW(TAG, "[BATCH] Invalid total_required: %d", total_required);
        return 0;
    }

    prv_batch_lock();
    s_batch_counter++;
    if (s_batch_counter == 0) {
        s_batch_counter = 1;
    }

    g_batch_tracker.batch_id = s_batch_counter;
    g_batch_tracker.total_required = (total_required > BATCH_MAX_FILES) ? BATCH_MAX_FILES : total_required;
    g_batch_tracker.scheduled_count = 0;
    g_batch_tracker.success_count = 0;
    g_batch_tracker.failed_count = 0;
    g_batch_tracker.all_scheduled = false;
    g_batch_tracker.completed = false;
    g_batch_tracker.restart_requested = false;
    memset(g_batch_tracker.file_status, BATCH_FILE_STATUS_IDLE, sizeof(g_batch_tracker.file_status));

    OSI_LOGI(TAG, "[BATCH] Started batch #%u with %d required files",
             g_batch_tracker.batch_id, g_batch_tracker.total_required);

    uint32_t id = g_batch_tracker.batch_id;
    prv_batch_unlock();
    return id;
}

void customer_audio_batch_finish_scheduling(uint32_t batch_id)
{
    prv_batch_lock();
    if (g_batch_tracker.batch_id == batch_id && batch_id != 0) {
        g_batch_tracker.all_scheduled = true;
        OSI_LOGI(TAG, "[BATCH #%u] All downloads scheduled (scheduled=%d, total=%d)",
                 batch_id, g_batch_tracker.scheduled_count, g_batch_tracker.total_required);
        customer_audio_batch_check_completion_locked();
    }
    prv_batch_unlock();
}

void customer_audio_batch_record_queue_failure(uint32_t batch_id)
{
    prv_batch_lock();
    if (g_batch_tracker.batch_id == batch_id && batch_id != 0) {
        g_batch_tracker.failed_count++;
        OSI_LOGE(TAG, "[BATCH #%u] Recorded queue/manifest failure (failed=%d, total=%d)",
                 batch_id, g_batch_tracker.failed_count, g_batch_tracker.total_required);
        customer_audio_batch_check_completion_locked();
    }
    prv_batch_unlock();
}

uint32_t customer_audio_batch_get_active_id(void)
{
    prv_batch_lock();
    uint32_t id = (!g_batch_tracker.completed) ? g_batch_tracker.batch_id : 0;
    prv_batch_unlock();
    return id;
}

static void customer_audio_download_callback(uint32_t msg_id, const char *save_path, void *user_data)
{
    audio_download_ctx_t *ctx = (audio_download_ctx_t *)user_data;

    if (msg_id == MSG_HTTP_DOWNLOAD_START) {
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download started: %s", save_path ? save_path : "");
        return;
    } else if (msg_id == MSG_HTTP_DOWNLOAD_PROGRESS) {
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download in progress: %s", save_path ? save_path : "");
        return;
    }

    bool restart = true;
    uint32_t batch_id = 0;
    int file_idx = -1;
    char lang_buf[16] = {0};

    if (ctx) {
        restart = ctx->restart_after_download;
        batch_id = ctx->batch_id;
        file_idx = ctx->file_index;
        if (ctx->lang[0] != '\0') {
            strncpy(lang_buf, ctx->lang, sizeof(lang_buf) - 1);
        }
        free(ctx);
        ctx = NULL;
    }

    if (msg_id == MSG_HTTP_DOWNLOAD_SUCCESS) {
        OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download SUCCESS: %s (batch=%u, idx=%d)",
                 save_path ? save_path : "", batch_id, file_idx);

        if (save_path && (strstr(save_path, ".zip") || strstr(save_path, ".ZIP"))) {
            OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Zip file detected: %s. Starting unzip to D:/FEMALE/...", save_path);
            int uz_ret = framework_unzip_start(save_path, "D:/FEMALE/", FRAMEWORK_UNZIP_TYPE_RESOURCES);
            if (uz_ret != 0) {
                OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] Failed to start unzip (%d)", uz_ret);
                if (batch_id != 0) {
                    customer_audio_batch_record_result(batch_id, file_idx, false);
                }
            } else {
                if (batch_id != 0) {
                    customer_audio_batch_record_result(batch_id, file_idx, true);
                }
            }
            return;
        }

        if (save_path && (strstr(save_path, "NetConfig") || strstr(save_path, ".txt") || strstr(save_path, ".TXT"))) {
            OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] File downloaded successfully to %s (no audio playback/reset needed)", save_path);
            if (batch_id != 0) {
                customer_audio_batch_record_result(batch_id, file_idx, true);
            }
            return;
        }

        if (lang_buf[0] != '\0') {
            customer_set_default_language(lang_buf);
            OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Download successful! Updated language to: %s (folder: %s)",
                     lang_buf, soundbox_get_current_audio_folder());
        }

        if (batch_id != 0) {
            customer_audio_batch_record_result(batch_id, file_idx, true);
        } else {
            if (save_path && save_path[0] != '\0') {
                // Play the newly downloaded MP3 file
                audio_resource_play_combo_async_mid_priority("", save_path, NULL, 1);
            }
            if (restart) {
                prv_trigger_restart();
            }
        }
    } else if (msg_id == MSG_HTTP_DOWNLOAD_FAIL) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] Download FAILED for: %s (batch=%u, idx=%d)",
                 save_path ? save_path : "", batch_id, file_idx);
        if (batch_id != 0) {
            customer_audio_batch_record_result(batch_id, file_idx, false);
        }
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

int customer_audio_download_handle_single_mp3(const char *url, const char *dst,
                                              const char *lang, bool restart_after_download)
{
    if (!url || url[0] == '\0') {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD-MP3] URL is empty");
        return -1;
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

    uint32_t batch_id = 0;
    int file_idx = -1;

    if (!restart_after_download) {
        prv_batch_lock();
        if (g_batch_tracker.batch_id != 0 && !g_batch_tracker.completed) {
            batch_id = g_batch_tracker.batch_id;
            file_idx = g_batch_tracker.scheduled_count;
            if (file_idx < BATCH_MAX_FILES) {
                g_batch_tracker.file_status[file_idx] = BATCH_FILE_STATUS_QUEUED;
            }
            g_batch_tracker.scheduled_count++;
        }
        prv_batch_unlock();
    }

    audio_download_ctx_t *ctx = (audio_download_ctx_t *)malloc(sizeof(audio_download_ctx_t));
    if (!ctx) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD-MP3] Failed to allocate download context");
        if (batch_id != 0) {
            customer_audio_batch_record_result(batch_id, file_idx, false);
        }
        return -2;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->restart_after_download = restart_after_download;
    ctx->batch_id = batch_id;
    ctx->file_index = file_idx;
    if (lang && lang[0] != '\0') {
        strncpy(ctx->lang, lang, sizeof(ctx->lang) - 1);
        ctx->lang[sizeof(ctx->lang) - 1] = '\0';
    }

    OSI_LOGI(TAG, "[AUDIO-DOWNLOAD-MP3] Queuing HTTP download: %s -> %s (restart=%d, batch=%u, idx=%d)",
             url, final_dst, (int)restart_after_download, batch_id, file_idx);

    int ret = framework_http_download_file_callback(
        url, final_dst, HTTP_DOWNLOAD_TYPE_FILE,
        customer_audio_download_callback, ctx);

    if (ret != 0) {
        OSI_LOGE(TAG, "[AUDIO-DOWNLOAD-MP3] Failed to queue download (%d)", ret);
        if (batch_id != 0) {
            customer_audio_batch_record_result(batch_id, file_idx, false);
        }
        free(ctx);
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
        return customer_audio_download_handle_single_mp3(url, dst, lang, true);
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
