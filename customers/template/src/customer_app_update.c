/**
 * @file customer_app_update.c
 * @brief Application & Firmware OTA update handler with SHA-256 verification
 */

#include "customer_app_update.h"
#include "framework_http.h"
#include "framework_audio.h"
#include "framework_log.h"
#include "fs_api.h"
#include "fota_api.h"
#include "osi_api.h"
#include "httpclient.h"
#include "mbedtls/sha256.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

#define TAG "APP_UPDATE"

#define OTA_STAGING_PATH        "D:/ota_staging.bin"
#define OTA_TARGET_PATH         FUPDATE_APP_FILE_NAME  /* "D:/FOTA_APP.bin" */
#define OTA_CHUNK_SIZE          (8 * 1024)
#define OTA_WORKER_STACK_SIZE   (24 * 1024)
#define OTA_WORKER_PRIORITY     120
#define OTA_MAX_CHUNK_RETRIES   3

typedef struct {
    bool initialized;
    bool busy;
    char pending_url[256];
    char pending_sha256[65];
    osiThread_t *worker_thread;
    osiSemaphore_t *trigger_sem;
    osiMutex_t *mutex;
} ota_update_mgr_t;

static ota_update_mgr_t s_ota_mgr = {0};

/* ============================================================================
 * Internal Utilities
 * ============================================================================ */

static char *prv_trim(char *str)
{
    if (!str) return "";
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return str;
}

static bool prv_is_valid_sha256_hex(const char *hex)
{
    if (!hex || strlen(hex) != 64) return false;
    for (int i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)hex[i])) {
            return false;
        }
    }
    return true;
}

static bool prv_sha256_matches(const char *digest1, const char *digest2)
{
    if (!digest1 || !digest2) return false;
    if (strlen(digest1) != 64 || strlen(digest2) != 64) return false;
    for (int i = 0; i < 64; i++) {
        if (tolower((unsigned char)digest1[i]) != tolower((unsigned char)digest2[i])) {
            return false;
        }
    }
    return true;
}

static int prv_calc_file_sha256(const char *path, char *out_hex65)
{
    if (!path || !out_hex65) return -1;

    int fd = fs_open(path, "rb");
    if (fd <= 0) {
        OSI_LOGE(TAG, "OTA: Failed to open file for SHA-256 calculation: %s (fd=%d)", path, fd);
        return -2;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0); // 0 = SHA-256

    unsigned char *buf = (unsigned char *)malloc(2048);
    if (!buf) {
        OSI_LOGE(TAG, "OTA: Failed to allocate SHA-256 read buffer");
        fs_close(fd);
        mbedtls_sha256_free(&ctx);
        return -3;
    }

    size_t nread = 0;
    while ((nread = fs_read(buf, 1, 2048, fd)) > 0) {
        mbedtls_sha256_update_ret(&ctx, buf, nread);
    }

    free(buf);
    fs_close(fd);

    unsigned char hash[32] = {0};
    mbedtls_sha256_finish_ret(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    for (int i = 0; i < 32; i++) {
        sprintf(out_hex65 + (i * 2), "%02x", hash[i]);
    }
    out_hex65[64] = '\0';
    return 0;
}

static int prv_copy_file(const char *src, const char *dst)
{
    int sfd = fs_open(src, "rb");
    if (sfd <= 0) return -1;

    int dfd = fs_open(dst, "wb+");
    if (dfd <= 0) {
        fs_close(sfd);
        return -2;
    }

    unsigned char *buf = (unsigned char *)malloc(4096);
    if (!buf) {
        fs_close(sfd);
        fs_close(dfd);
        return -3;
    }

    size_t n = 0;
    int ret = 0;
    while ((n = fs_read(buf, 1, 4096, sfd)) > 0) {
        while (audio_resource_is_playing()) {
            osiThreadSleep(20);
        }
        size_t written = fs_write(buf, 1, n, dfd);
        if (written != n) {
            ret = -4;
            break;
        }
    }

    fs_sync(dfd);
    fs_close(sfd);
    fs_close(dfd);
    free(buf);
    return ret;
}

static int prv_ota_download_file(const char *url, const char *save_path, uint32_t *out_size)
{
    if (!url || !save_path || !out_size) {
        return -1;
    }
    *out_size = 0;

    char current_url[256];
    strncpy(current_url, url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';

    char *hdr_buf = (char *)malloc(1024);
    if (!hdr_buf) {
        OSI_LOGE(TAG, "OTA: Failed to allocate header buffer");
        return -2;
    }

    char *chunk_buf = (char *)malloc(OTA_CHUNK_SIZE + 1);
    if (!chunk_buf) {
        OSI_LOGE(TAG, "OTA: Failed to allocate chunk buffer (%d bytes)", OTA_CHUNK_SIZE);
        free(hdr_buf);
        return -3;
    }

    // Step 1: Probe remote file size using Range: bytes=0-0
    uint32_t total_size = 0;
    for (int probe_retry = 0; probe_retry < 3; probe_retry++) {
        httpclient_t client = {0};
        httpclient_data_t client_data = {0};
        memset(hdr_buf, 0, 1024);

        client.header = "Range: bytes=0-0\r\nAccept: */*\r\nConnection: close\r\n";
        client.verify_mode = 0;
        client_data.response_buf = chunk_buf;
        client_data.response_buf_len = 4;
        client_data.header_buf = hdr_buf;
        client_data.header_buf_len = 1024;

        HTTPCLIENT_RESULT res = httpclient_get(&client, current_url, &client_data);
        int resp_code = client.response_code;

        if ((resp_code == 301 || resp_code == 302 || resp_code == 307) && res >= 0) {
            int loc_pos = 0, loc_len = 0;
            if (httpclient_get_response_header_value(hdr_buf, "Location", &loc_pos, &loc_len) == 0 &&
                loc_len > 0 && loc_len < (int)sizeof(current_url)) {
                memcpy(current_url, hdr_buf + loc_pos, loc_len);
                current_url[loc_len] = '\0';
                OSI_LOGI(TAG, "OTA: Redirected (%d) -> %s", resp_code, current_url);
                probe_retry--;
                continue;
            }
        }

        if (res >= 0 && (resp_code == 206 || resp_code == 200)) {
            int val_pos = 0, val_len = 0;
            if (httpclient_get_response_header_value(hdr_buf, "Content-Range", &val_pos, &val_len) == 0 && val_len > 0) {
                const char *slash = memchr(hdr_buf + val_pos, '/', val_len);
                if (slash) {
                    total_size = strtoul(slash + 1, NULL, 10);
                }
            }
            if (total_size == 0 && httpclient_get_response_header_value(hdr_buf, "Content-Length", &val_pos, &val_len) == 0 && val_len > 0) {
                total_size = strtoul(hdr_buf + val_pos, NULL, 10);
            }
            if (total_size > 0) {
                OSI_LOGI(TAG, "OTA: Discovered file size: %lu bytes", (unsigned long)total_size);
                break;
            }
        }
        osiThreadSleep(1000);
    }

    if (total_size == 0) {
        OSI_LOGE(TAG, "OTA: Failed to discover remote file size from URL");
        free(chunk_buf);
        free(hdr_buf);
        return -4;
    }

    // Step 2: Open target staging file
    fs_remove(save_path);
    int fd = fs_open(save_path, "wb+");
    if (fd <= 0) {
        OSI_LOGE(TAG, "OTA: Failed to create staging file: %s (fd=%d)", save_path, fd);
        free(chunk_buf);
        free(hdr_buf);
        return -5;
    }

    // Step 3: Stream chunks with Range requests
    uint32_t offset = 0;
    while (offset < total_size) {
        uint32_t chunk = total_size - offset;
        if (chunk > OTA_CHUNK_SIZE) {
            chunk = OTA_CHUNK_SIZE;
        }

        char range_header[128];
        snprintf(range_header, sizeof(range_header),
                 "Range: bytes=%lu-%lu\r\nAccept: */*\r\nConnection: close\r\n",
                 (unsigned long)offset, (unsigned long)(offset + chunk - 1));

        bool chunk_ok = false;
        for (int retry = 0; retry < OTA_MAX_CHUNK_RETRIES; retry++) {
            if (retry > 0) {
                OSI_LOGW(TAG, "OTA: Retrying chunk offset %lu (attempt %d/%d)",
                         (unsigned long)offset, retry + 1, OTA_MAX_CHUNK_RETRIES);
                osiThreadSleep(1000);
            }

            httpclient_t client = {0};
            httpclient_data_t client_data = {0};
            memset(hdr_buf, 0, 1024);

            client.header = range_header;
            client.verify_mode = 0; // VERIFY_NONE
            client_data.response_buf = chunk_buf;
            client_data.response_buf_len = chunk + 1;
            client_data.header_buf = hdr_buf;
            client_data.header_buf_len = 1024;
            client_data.response_content_len = 0;

            HTTPCLIENT_RESULT res = httpclient_get(&client, current_url, &client_data);
            int resp_code = client.response_code;

            if ((resp_code == 301 || resp_code == 302 || resp_code == 307) && res >= 0) {
                int loc_pos = 0, loc_len = 0;
                if (httpclient_get_response_header_value(hdr_buf, "Location", &loc_pos, &loc_len) == 0 &&
                    loc_len > 0 && loc_len < (int)sizeof(current_url)) {
                    memcpy(current_url, hdr_buf + loc_pos, loc_len);
                    current_url[loc_len] = '\0';
                    OSI_LOGI(TAG, "OTA: Redirected (%d) -> %s", resp_code, current_url);
                    retry--;
                    continue;
                }
            }

            if ((res == HTTPCLIENT_OK || res == HTTPCLIENT_RETRIEVE_MORE_DATA) &&
                (resp_code == 206 || (resp_code == 200 && offset == 0)) &&
                client_data.response_content_len > 0) {

                while (audio_resource_is_playing()) {
                    osiThreadSleep(20);
                }

                size_t written = fs_write(chunk_buf, 1, client_data.response_content_len, fd);
                if (written != (size_t)client_data.response_content_len) {
                    OSI_LOGE(TAG, "OTA: Flash write error: wrote=%u, expected=%d",
                             (unsigned)written, client_data.response_content_len);
                    break;
                }

                offset += client_data.response_content_len;
                chunk_ok = true;

                int percent = (int)((offset * 100ULL) / total_size);
                if (percent % 25 == 0 || offset == total_size) {
                    OSI_LOGI(TAG, "OTA: Download progress: %d%% (%lu/%lu bytes)",
                             percent, (unsigned long)offset, (unsigned long)total_size);
                }
                break;
            } else {
                OSI_LOGW(TAG, "OTA: Chunk fetch failed: res=%d, code=%d, len=%d",
                         (int)res, resp_code, client_data.response_content_len);
            }
        }

        if (!chunk_ok) {
            OSI_LOGE(TAG, "OTA: Failed to download chunk at offset %lu after retries", (unsigned long)offset);
            fs_close(fd);
            fs_remove(save_path);
            free(chunk_buf);
            free(hdr_buf);
            return -6;
        }
    }

    fs_sync(fd);
    fs_close(fd);
    free(chunk_buf);
    free(hdr_buf);

    *out_size = offset;
    return 0;
}

/* ============================================================================
 * Worker Task Implementation
 * ============================================================================ */

static void prv_ota_worker_task(void *param)
{
    (void)param;
    OSI_LOGI(TAG, "OTA: Worker task started");

    while (1) {
        osiSemaphoreAcquire(s_ota_mgr.trigger_sem);

        char url[256] = {0};
        char expected_sha[65] = {0};

        osiMutexLock(s_ota_mgr.mutex);
        strncpy(url, s_ota_mgr.pending_url, sizeof(url) - 1);
        strncpy(expected_sha, s_ota_mgr.pending_sha256, sizeof(expected_sha) - 1);
        osiMutexUnlock(s_ota_mgr.mutex);

        OSI_LOGI(TAG, "==================================================");
        OSI_LOGI(TAG, "OTA: Processing OTA Update Request");
        OSI_LOGI(TAG, "OTA: URL=%s", url);
        OSI_LOGI(TAG, "OTA: Expected SHA-256=%s", expected_sha);
        OSI_LOGI(TAG, "==================================================");

        // Audio feedback
        audio_resource_play_combo_async_mid_priority("", "upgrade-ing.mp3", NULL, 1);

        // Step 2: Download the application binary
        OSI_LOGI(TAG, "OTA: Download started");
        fs_remove(OTA_STAGING_PATH);

        uint32_t downloaded_size = 0;
        int dl_ret = prv_ota_download_file(url, OTA_STAGING_PATH, &downloaded_size);
        if (dl_ret != 0 || downloaded_size == 0) {
            OSI_LOGE(TAG, "OTA: Download failed (ret=%d, size=%lu)", dl_ret, (unsigned long)downloaded_size);
            fs_remove(OTA_STAGING_PATH);
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            goto cleanup_and_idle;
        }

        OSI_LOGI(TAG, "OTA: Download completed");
        OSI_LOGI(TAG, "OTA: Downloaded file size: %lu bytes", (unsigned long)downloaded_size);

        // Step 3: Verify SHA-256
        OSI_LOGI(TAG, "OTA: SHA-256 calculation started");
        OSI_LOGI(TAG, "OTA: Expected SHA-256: %s", expected_sha);

        char calc_sha[65] = {0};
        int sha_ret = prv_calc_file_sha256(OTA_STAGING_PATH, calc_sha);
        if (sha_ret != 0) {
            OSI_LOGE(TAG, "OTA: Failed to calculate SHA-256 for downloaded file (ret=%d)", sha_ret);
            fs_remove(OTA_STAGING_PATH);
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            goto cleanup_and_idle;
        }

        OSI_LOGI(TAG, "OTA: Calculated SHA-256: %s", calc_sha);

        if (!prv_sha256_matches(calc_sha, expected_sha)) {
            OSI_LOGE(TAG, "OTA: SHA-256 verification failed (checksum mismatch)");
            OSI_LOGE(TAG, "OTA: Expected:   %s", expected_sha);
            OSI_LOGE(TAG, "OTA: Calculated: %s", calc_sha);
            fs_remove(OTA_STAGING_PATH);
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            goto cleanup_and_idle;
        }

        OSI_LOGI(TAG, "OTA: SHA-256 verification successful");

        // Step 4: Install the verified application
        OSI_LOGI(TAG, "OTA: Installation started");

        fs_remove(OTA_TARGET_PATH);
        if (fs_rename(OTA_STAGING_PATH, OTA_TARGET_PATH) != 0) {
            OSI_LOGI(TAG, "OTA: fs_rename failed, falling back to file copy");
            if (prv_copy_file(OTA_STAGING_PATH, OTA_TARGET_PATH) != 0) {
                OSI_LOGE(TAG, "OTA: Failed to copy staging binary to target FOTA location");
                fs_remove(OTA_STAGING_PATH);
                fs_remove(OTA_TARGET_PATH);
                audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
                goto cleanup_and_idle;
            }
            fs_remove(OTA_STAGING_PATH);
        }

        if (fs_size(OTA_TARGET_PATH) == 0) {
            OSI_LOGE(TAG, "OTA: Target FOTA file is missing or 0 bytes");
            fs_remove(OTA_TARGET_PATH);
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            goto cleanup_and_idle;
        }

        bool fota_ok = fupdateAppSetReady(OTA_TARGET_PATH);
        if (!fota_ok) {
            OSI_LOGE(TAG, "OTA: Installation failed (fupdateAppSetReady returned false)");
            fs_remove(OTA_TARGET_PATH);
            audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
            goto cleanup_and_idle;
        }

        OSI_LOGI(TAG, "OTA: Installation successful");
        audio_resource_play_combo_async_mid_priority("", "upgrade-success.mp3", NULL, 1);

        // Step 5: Restart after successful installation
        OSI_LOGI(TAG, "OTA: Restarting device");
        osiThreadSleep(1500);

        osiMutexLock(s_ota_mgr.mutex);
        s_ota_mgr.busy = false;
        osiMutexUnlock(s_ota_mgr.mutex);

        osiSysReset();
        continue;

cleanup_and_idle:
        fs_remove(OTA_STAGING_PATH);
        osiMutexLock(s_ota_mgr.mutex);
        s_ota_mgr.busy = false;
        s_ota_mgr.pending_url[0] = '\0';
        s_ota_mgr.pending_sha256[0] = '\0';
        osiMutexUnlock(s_ota_mgr.mutex);
    }
}

/* ============================================================================
 * Public APIs
 * ============================================================================ */

int customer_app_update_init(void)
{
    if (s_ota_mgr.initialized) {
        return 0;
    }

    s_ota_mgr.mutex = osiMutexCreate();
    s_ota_mgr.trigger_sem = osiSemaphoreCreate(1, 0);

    s_ota_mgr.worker_thread = osiThreadCreate(
        "ota_worker",
        prv_ota_worker_task,
        NULL,
        OTA_WORKER_PRIORITY,
        OTA_WORKER_STACK_SIZE
    );

    if (!s_ota_mgr.worker_thread) {
        OSI_LOGE(TAG, "OTA: Failed to create OTA worker thread");
        return -1;
    }

    s_ota_mgr.initialized = true;
    OSI_LOGI(TAG, "OTA: Secure OTA Module initialized successfully");
    return 0;
}

int customer_app_update_start_secure(const char *url, const char *expected_sha256)
{
    if (!url || url[0] == '\0' || !expected_sha256 || expected_sha256[0] == '\0') {
        OSI_LOGE(TAG, "OTA: Invalid parameters for start_secure");
        return -1;
    }

    if (!s_ota_mgr.initialized) {
        customer_app_update_init();
    }

    osiMutexLock(s_ota_mgr.mutex);
    if (s_ota_mgr.busy) {
        osiMutexUnlock(s_ota_mgr.mutex);
        OSI_LOGW(TAG, "OTA: Another update operation is already in progress, rejecting concurrent request");
        return -2;
    }

    s_ota_mgr.busy = true;
    strncpy(s_ota_mgr.pending_url, url, sizeof(s_ota_mgr.pending_url) - 1);
    strncpy(s_ota_mgr.pending_sha256, expected_sha256, sizeof(s_ota_mgr.pending_sha256) - 1);
    osiMutexUnlock(s_ota_mgr.mutex);

    osiSemaphoreRelease(s_ota_mgr.trigger_sem);
    return 0;
}

int customer_app_update_handle_mqtt_command(const char *payload)
{
    if (!payload || payload[0] == '\0') {
        return -1;
    }

    OSI_LOGI(TAG, "OTA: Command received");

    if (!s_ota_mgr.initialized) {
        customer_app_update_init();
    }

    char *copy = (char *)malloc(strlen(payload) + 1);
    if (!copy) {
        OSI_LOGE(TAG, "OTA: Memory allocation failed for command parsing");
        return -2;
    }
    strcpy(copy, payload);

    // Format: ota-update | <url> | <sha256_hash>
    char *tokens[3] = {0};
    int count = 0;
    char *p = copy;

    while (*p && count < 3) {
        tokens[count++] = p;
        char *pipe = strchr(p, '|');
        if (pipe) {
            *pipe = '\0';
            p = pipe + 1;
        } else {
            break;
        }
    }

    for (int i = 0; i < count; i++) {
        tokens[i] = prv_trim(tokens[i]);
    }

    if (count < 3 ||
        (strcasecmp(tokens[0], "ota-update") != 0 && strcasecmp(tokens[0], "ota_update") != 0)) {
        OSI_LOGE(TAG, "OTA: Command fields validation failed: expected 'ota-update | <url> | <sha256>'");
        free(copy);
        return -3;
    }

    const char *url = tokens[1];
    const char *sha256_hex = tokens[2];

    if (strlen(url) == 0 || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
        OSI_LOGE(TAG, "OTA: Command fields validation failed: invalid URL '%s'", url);
        free(copy);
        return -4;
    }

    if (!prv_is_valid_sha256_hex(sha256_hex)) {
        OSI_LOGE(TAG, "OTA: Command fields validation failed: invalid SHA-256 '%s' (must be 64 hex characters)", sha256_hex);
        free(copy);
        return -5;
    }

    OSI_LOGI(TAG, "OTA: Command fields validated");

    osiMutexLock(s_ota_mgr.mutex);
    if (s_ota_mgr.busy) {
        osiMutexUnlock(s_ota_mgr.mutex);
        OSI_LOGW(TAG, "OTA: Another update operation is already in progress, rejecting concurrent request");
        free(copy);
        return -6;
    }

    s_ota_mgr.busy = true;
    strncpy(s_ota_mgr.pending_url, url, sizeof(s_ota_mgr.pending_url) - 1);
    strncpy(s_ota_mgr.pending_sha256, sha256_hex, sizeof(s_ota_mgr.pending_sha256) - 1);
    osiMutexUnlock(s_ota_mgr.mutex);

    free(copy);
    osiSemaphoreRelease(s_ota_mgr.trigger_sem);
    return 0;
}

/* ============================================================================
 * Legacy / Backward-Compatible APIs
 * ============================================================================ */

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
