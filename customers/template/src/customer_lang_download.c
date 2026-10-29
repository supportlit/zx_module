/**
 * @file customer_lang_download.c
 * @brief TMS Language Pack Download Manager Implementation
 */

#include "customer_lang_download.h"
#include "customer_language_ack.h"
#include "customer_change_language_ack.h"
#include "customer_mqtt_handler.h"
#include "soundbox_language.h"
#include "framework_http.h"
#include "framework_unzip.h"
#include "framework_device_config.h"
#include "framework_network.h"
#include "framework_log.h"
#include "framework_audio.h"
#include "fs_api.h"
#include "httpclient.h"
#include "cJSON.h"
#include "osi_api.h"
#include "mbedtls/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define TAG "TMS_LANG"

#define LANG_GET_DETAILS_URL "https://1.7.140.58/pos/TMS/LanguageFile/GetLanguageFileDetails"
#define LANG_DOWNLOAD_FILE_URL "https://1.7.140.58/pos/TMS/LanguageFile/DownloadLanguageFile"

#define LANG_MAX_ZIP_FILES 8
#define LANG_TEMP_DIR "D:/lang_temp"
#define LANG_STAGING_DIR "D:/lang_staging"
#define LANG_MAX_RETRIES 3
#define LANG_DOWNLOAD_TIMEOUT_MS 60000
#define LANG_UNZIP_TIMEOUT_MS 60000
#define LANG_HTTP_TIMEOUT_MS 30000

typedef struct
{
    char url[256];
    uint32_t size;
    char uuid[64];
    char sign[65];
} lang_zip_info_t;

typedef struct
{
    double request_id;
    char request_id_str[32];
    char biz_type[16];
    char device_id[32];
    char newlanguage[16];
    int zip_count;
    lang_zip_info_t zip_files[LANG_MAX_ZIP_FILES];
} lang_metadata_t;

typedef struct
{
    bool initialized;
    bool busy;
    char pending_req_id[32];
    char pending_url[256];

    osiThread_t *worker_thread;
    osiSemaphore_t *trigger_sem;
    osiSemaphore_t *http_sem;
    osiSemaphore_t *dl_sem;
    osiSemaphore_t *unzip_sem;
    osiMutex_t *mutex;

    // HTTP Async Callback Fields
    int http_result;
    int http_status_code;
    char *http_response_data;
    uint32_t http_response_len;

    // Download / Unzip result codes
    int dl_result_msg_id;
    int unzip_result_msg_id;
} lang_download_mgr_t;

static lang_download_mgr_t s_mgr = {0};

/* ============================================================================
 * Helper Utilities
 * ============================================================================ */

static void prv_lock(void)
{
    if (s_mgr.mutex)
    {
        osiMutexLock(s_mgr.mutex);
    }
}

static void prv_unlock(void)
{
    if (s_mgr.mutex)
    {
        osiMutexUnlock(s_mgr.mutex);
    }
}

static char *prv_trim(char *str)
{
    if (!str)
        return "";
    while (*str && isspace((unsigned char)*str))
        str++;
    if (*str == '\0')
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }
    return str;
}

static bool prv_str_ends_with_case(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return false;
    size_t slen = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > slen)
        return false;
    const char *p = str + (slen - suffix_len);
    for (size_t i = 0; i < suffix_len; i++)
    {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)suffix[i]))
        {
            return false;
        }
    }
    return true;
}

static bool prv_sha256_matches(const char *digest1, const char *digest2)
{
    if (!digest1 || !digest2)
        return false;
    if (strlen(digest1) != 64 || strlen(digest2) != 64)
        return false;
    for (int i = 0; i < 64; i++)
    {
        if (tolower((unsigned char)digest1[i]) != tolower((unsigned char)digest2[i]))
        {
            return false;
        }
    }
    return true;
}

static int prv_calc_file_sha256(const char *path, char *out_hex65)
{
    if (!path || !out_hex65)
        return -1;

    int fd = fs_open(path, "rb");
    if (fd <= 0)
    {
        OSI_LOGE(TAG, "[SHA256] Failed to open file for read: %s (fd=%d)", path, fd);
        return -2;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0); // 0 = SHA-256 (not SHA-224)

    unsigned char *buf = (unsigned char *)malloc(2048);
    if (!buf)
    {
        fs_close(fd);
        mbedtls_sha256_free(&ctx);
        OSI_LOGE(TAG, "[SHA256] Buffer allocation failed");
        return -3;
    }

    int bytes_read = 0;
    while ((bytes_read = (int)fs_read(buf, 1, 2048, fd)) > 0)
    {
        mbedtls_sha256_update_ret(&ctx, buf, (size_t)bytes_read);
    }
    free(buf);
    fs_close(fd);

    unsigned char digest[32];
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    for (int i = 0; i < 32; i++)
    {
        sprintf(&out_hex65[i * 2], "%02x", digest[i]);
    }
    out_hex65[64] = '\0';
    return 0;
}

static void prv_clean_dir(const char *dir_path)
{
    if (!dir_path)
        return;
    uint32_t dir_handle = fs_opendir(dir_path);
    if (!dir_handle)
        return;

    DirFileInfo_t file_info;
    while (fs_readdir((int)dir_handle, &file_info) == 1)
    {
        if (file_info.file_name[0] == '\0' ||
            strcmp(file_info.file_name, ".") == 0 ||
            strcmp(file_info.file_name, "..") == 0)
        {
            continue;
        }
        char full_path[320];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, file_info.file_name);
        fs_remove(full_path);
    }
    fs_closedir((int)dir_handle);
}

static int prv_count_audio_files(const char *dir_path)
{
    if (!dir_path)
        return 0;
    uint32_t dir_handle = fs_opendir(dir_path);
    if (!dir_handle)
    {
        OSI_LOGW(TAG, "[FS] Cannot open directory: %s", dir_path);
        return 0;
    }

    int count = 0;
    DirFileInfo_t file_info;
    while (fs_readdir((int)dir_handle, &file_info) == 1)
    {
        if (file_info.file_name[0] == '\0' ||
            strcmp(file_info.file_name, ".") == 0 ||
            strcmp(file_info.file_name, "..") == 0)
        {
            continue;
        }
        if (prv_str_ends_with_case(file_info.file_name, ".mp3") ||
            prv_str_ends_with_case(file_info.file_name, ".wav"))
        {
            count++;
        }
    }
    fs_closedir((int)dir_handle);
    return count;
}

static int prv_copy_or_move_file(const char *src_path, const char *dst_path)
{
    // Try atomic rename first
    if (fs_rename(src_path, dst_path) == 0)
    {
        return 0;
    }

    // Fallback: chunked copy
    int src_fd = fs_open(src_path, "rb");
    if (src_fd <= 0)
        return -1;

    int dst_fd = fs_open(dst_path, "wb+");
    if (dst_fd <= 0)
    {
        fs_close(src_fd);
        return -2;
    }

    char *buf = (char *)malloc(2048);
    if (!buf)
    {
        fs_close(src_fd);
        fs_close(dst_fd);
        return -3;
    }

    int r = 0;
    int ret = 0;
    while ((r = (int)fs_read(buf, 1, 2048, src_fd)) > 0)
    {
        if ((int)fs_write(buf, 1, (size_t)r, dst_fd) != r)
        {
            ret = -4;
            break;
        }
    }

    free(buf);
    fs_close(src_fd);
    fs_close(dst_fd);

    if (ret == 0)
    {
        fs_remove(src_path);
    }
    else
    {
        fs_remove(dst_path);
    }
    return ret;
}

static int prv_deploy_staging_to_target(const char *staging_dir, const char *target_dir)
{
    fs_makedir(target_dir);

    uint32_t dir_handle = fs_opendir(staging_dir);
    if (!dir_handle)
    {
        OSI_LOGE(TAG, "[DEPLOY] Cannot open staging dir: %s", staging_dir);
        return -1;
    }

    DirFileInfo_t file_info;
    int moved_count = 0;
    while (fs_readdir((int)dir_handle, &file_info) == 1)
    {
        if (file_info.file_name[0] == '\0' ||
            strcmp(file_info.file_name, ".") == 0 ||
            strcmp(file_info.file_name, "..") == 0)
        {
            continue;
        }

        char src_file[320];
        char dst_file[320];
        snprintf(src_file, sizeof(src_file), "%s/%s", staging_dir, file_info.file_name);

        size_t tlen = strlen(target_dir);
        if (tlen > 0 && (target_dir[tlen - 1] == '/' || target_dir[tlen - 1] == '\\'))
        {
            snprintf(dst_file, sizeof(dst_file), "%s%s", target_dir, file_info.file_name);
        }
        else
        {
            snprintf(dst_file, sizeof(dst_file), "%s/%s", target_dir, file_info.file_name);
        }

        int ret = prv_copy_or_move_file(src_file, dst_file);
        if (ret == 0)
        {
            moved_count++;
            OSI_LOGI(TAG, "[DEPLOY] Installed audio: %s -> %s", src_file, dst_file);
        }
        else
        {
            OSI_LOGE(TAG, "[DEPLOY] Failed to move %s -> %s (err=%d)", src_file, dst_file, ret);
        }
    }

    fs_closedir((int)dir_handle);
    return (moved_count > 0) ? 0 : -2;
}

/* ============================================================================
 * HTTP & Framework Callbacks
 * ============================================================================ */

static void prv_http_req_callback(http_response_t *response, void *user_data)
{
    (void)user_data;
    if (!response)
    {
        OSI_LOGE(TAG, "[HTTP-CB] Null HTTP response structure");
        s_mgr.http_result = -1;
        s_mgr.http_status_code = 0;
        if (s_mgr.http_sem)
            osiSemaphoreRelease(s_mgr.http_sem);
        return;
    }

    s_mgr.http_result = (int)response->result;
    s_mgr.http_status_code = response->status_code;

    if (s_mgr.http_response_data)
    {
        free(s_mgr.http_response_data);
        s_mgr.http_response_data = NULL;
    }
    s_mgr.http_response_len = 0;

    if (response->response_data && response->response_len > 0)
    {
        s_mgr.http_response_data = (char *)malloc(response->response_len + 1);
        if (s_mgr.http_response_data)
        {
            memcpy(s_mgr.http_response_data, response->response_data, response->response_len);
            s_mgr.http_response_data[response->response_len] = '\0';
            s_mgr.http_response_len = response->response_len;
        }
    }

    OSI_LOGI(TAG, "[HTTP-CB] result=%d, status=%d, len=%u",
             s_mgr.http_result, s_mgr.http_status_code, s_mgr.http_response_len);

    if (s_mgr.http_sem)
    {
        osiSemaphoreRelease(s_mgr.http_sem);
    }
}

static void __attribute__((unused)) prv_http_download_callback(uint32_t msg_id, const char *save_path, void *user_data)
{
    (void)user_data;
    OSI_LOGI(TAG, "[DL-CB] msg_id=%u, path=%s", msg_id, save_path ? save_path : "(null)");
    s_mgr.dl_result_msg_id = (int)msg_id;
    if (s_mgr.dl_sem)
    {
        osiSemaphoreRelease(s_mgr.dl_sem);
    }
}

void customer_lang_download_handle_unzip_msg(framework_msg_t *msg)
{
    if (!msg)
        return;
    if (!s_mgr.busy)
        return;

    framework_unzip_type_t type = (framework_unzip_type_t)msg->param1;
    if (type != FRAMEWORK_UNZIP_TYPE_RESOURCES)
        return;

    if (msg->id == MSG_UNZIP_SUCCESS || msg->id == MSG_UNZIP_FAIL)
    {
        OSI_LOGI(TAG, "[UNZIP-CB] Received msg_id=%d (param2=%d, param3=%s)",
                 msg->id, msg->param2, (const char *)(uintptr_t)msg->param3);
        s_mgr.unzip_result_msg_id = (int)msg->id;
        if (s_mgr.unzip_sem)
        {
            osiSemaphoreRelease(s_mgr.unzip_sem);
        }
    }
}

/* ============================================================================
 * Parsing & Execution Logic
 * ============================================================================ */

static int prv_parse_metadata_json(const char *json_str, double expected_req_id, lang_metadata_t *meta)
{
    if (!json_str || !meta)
        return -1;
    memset(meta, 0, sizeof(*meta));

    cJSON *root = cJSON_Parse(json_str);
    if (!root)
    {
        OSI_LOGE(TAG, "[PARSE] Failed to parse JSON metadata: %s", json_str);
        return -2;
    }

    cJSON *j_code = cJSON_GetObjectItem(root, "ResponseCode");
    if (!j_code || !cJSON_IsString(j_code) || strcmp(j_code->valuestring, "00") != 0)
    {
        OSI_LOGE(TAG, "[PARSE] Invalid or missing ResponseCode (expected '00')");
        cJSON_Delete(root);
        return -3;
    }

    cJSON *j_req = cJSON_GetObjectItem(root, "request_id");
    if (j_req)
    {
        if (cJSON_IsNumber(j_req))
        {
            meta->request_id = j_req->valuedouble;
            snprintf(meta->request_id_str, sizeof(meta->request_id_str), "%.0f", meta->request_id);
        }
        else if (cJSON_IsString(j_req) && j_req->valuestring)
        {
            meta->request_id = strtod(j_req->valuestring, NULL);
            strncpy(meta->request_id_str, j_req->valuestring, sizeof(meta->request_id_str) - 1);
        }
    }

    if (expected_req_id > 0 && meta->request_id > 0)
    {
        if ((uint64_t)expected_req_id != (uint64_t)meta->request_id)
        {
            OSI_LOGE(TAG, "[PARSE] Request ID mismatch: MQTT=%.0f, JSON=%.0f",
                     expected_req_id, meta->request_id);
            cJSON_Delete(root);
            return -4;
        }
    }

    cJSON *j_biz = cJSON_GetObjectItem(root, "biz_type");
    if (j_biz && cJSON_IsString(j_biz))
    {
        strncpy(meta->biz_type, j_biz->valuestring, sizeof(meta->biz_type) - 1);
    }

    cJSON *j_dev = cJSON_GetObjectItem(root, "deviceid");
    if (!j_dev)
        j_dev = cJSON_GetObjectItem(root, "deviceId");
    if (j_dev && cJSON_IsString(j_dev))
    {
        strncpy(meta->device_id, j_dev->valuestring, sizeof(meta->device_id) - 1);
    }
    else
    {
        framework_device_config_get_sn(meta->device_id, sizeof(meta->device_id));
    }

    cJSON *j_lang = cJSON_GetObjectItem(root, "newlanguage");
    if (!j_lang)
        j_lang = cJSON_GetObjectItem(root, "language");
    if (j_lang)
    {
        if (cJSON_IsString(j_lang))
        {
            strncpy(meta->newlanguage, j_lang->valuestring, sizeof(meta->newlanguage) - 1);
        }
        else if (cJSON_IsNumber(j_lang))
        {
            snprintf(meta->newlanguage, sizeof(meta->newlanguage), "%d", j_lang->valueint);
        }
    }
    if (meta->newlanguage[0] == '\0')
    {
        OSI_LOGE(TAG, "[PARSE] Missing newlanguage in JSON response");
        cJSON_Delete(root);
        return -5;
    }

    cJSON *j_audio_inf = cJSON_GetObjectItem(root, "audio_inf");
    if (!j_audio_inf || !cJSON_IsArray(j_audio_inf))
    {
        OSI_LOGE(TAG, "[PARSE] Missing audio_inf array in JSON response");
        cJSON_Delete(root);
        return -6;
    }

    int num_items = cJSON_GetArraySize(j_audio_inf);
    if (num_items <= 0)
    {
        OSI_LOGE(TAG, "[PARSE] audio_inf array is empty");
        cJSON_Delete(root);
        return -7;
    }

    if (num_items > LANG_MAX_ZIP_FILES)
    {
        OSI_LOGW(TAG, "[PARSE] audio_inf items (%d) exceeds max (%d), truncating",
                 num_items, LANG_MAX_ZIP_FILES);
        num_items = LANG_MAX_ZIP_FILES;
    }

    for (int i = 0; i < num_items; i++)
    {
        cJSON *item = cJSON_GetArrayItem(j_audio_inf, i);
        if (!item)
            continue;

        cJSON *j_url = cJSON_GetObjectItem(item, "url");
        cJSON *j_size = cJSON_GetObjectItem(item, "size");
        cJSON *j_uuid = cJSON_GetObjectItem(item, "uuid");
        cJSON *j_sign = cJSON_GetObjectItem(item, "sign");

        if (j_url && cJSON_IsString(j_url) && j_url->valuestring)
        {
            strncpy(meta->zip_files[i].url, j_url->valuestring, sizeof(meta->zip_files[i].url) - 1);
        }
        if (j_size && cJSON_IsNumber(j_size))
        {
            meta->zip_files[i].size = (uint32_t)j_size->valuedouble;
        }
        if (j_uuid && cJSON_IsString(j_uuid) && j_uuid->valuestring)
        {
            strncpy(meta->zip_files[i].uuid, j_uuid->valuestring, sizeof(meta->zip_files[i].uuid) - 1);
        }
        if (j_sign && cJSON_IsString(j_sign) && j_sign->valuestring)
        {
            strncpy(meta->zip_files[i].sign, j_sign->valuestring, sizeof(meta->zip_files[i].sign) - 1);
        }

        // Validate each item
        if (meta->zip_files[i].size == 0 ||
            meta->zip_files[i].uuid[0] == '\0' ||
            strlen(meta->zip_files[i].sign) != 64)
        {
            OSI_LOGE(TAG, "[PARSE] Invalid audio_inf entry #%d: size=%u, uuid=%s, sign_len=%d",
                     i, meta->zip_files[i].size, meta->zip_files[i].uuid, (int)strlen(meta->zip_files[i].sign));
            cJSON_Delete(root);
            return -8;
        }

        OSI_LOGI(TAG, "[PARSE] Entry #%d: uuid=%s, size=%u, sign=%.8s..., url=%s",
                 i, meta->zip_files[i].uuid, meta->zip_files[i].size,
                 meta->zip_files[i].sign, meta->zip_files[i].url);
    }

    meta->zip_count = num_items;
    cJSON_Delete(root);
    return 0;
}

static int prv_fetch_metadata(const char *url, const char *req_id_str, lang_metadata_t *out_meta)
{
    double req_id = (req_id_str && req_id_str[0]) ? strtod(req_id_str, NULL) : 0;
    s_mgr.http_result = -1;
    s_mgr.http_status_code = 0;

    if (url && url[0] != '\0')
    {
        OSI_LOGI(TAG, "[FETCH-META] Fetching test metadata via GET from: %s", url);
        http_request_t req = {0};
        req.method = HTTP_METHOD_GET;
        strncpy(req.url, url, sizeof(req.url) - 1);
        req.timeout_ms = LANG_HTTP_TIMEOUT_MS;

        int ret = framework_http_request_async(&req, prv_http_req_callback);
        if (ret != 0)
        {
            OSI_LOGE(TAG, "[FETCH-META] Failed to queue async GET (%d)", ret);
            return -1;
        }
    }
    else
    {
        char sn[32] = {0};
        framework_device_config_get_sn(sn, sizeof(sn));
        cJSON *req_json = cJSON_CreateObject();
        cJSON_AddStringToObject(req_json, "biz_type", "F");
        cJSON_AddStringToObject(req_json, "deviceId", sn);
        char *req_body = cJSON_PrintUnformatted(req_json);
        cJSON_Delete(req_json);

        if (!req_body)
        {
            OSI_LOGE(TAG, "[FETCH-META] Failed to construct JSON request body");
            return -2;
        }

        OSI_LOGI(TAG, "[FETCH-META] POSTing to production details API %s (length=%u)",
                 LANG_GET_DETAILS_URL, (unsigned int)strlen(req_body));
        log_string_in_chunks_with_label(TAG, "LANG_DETAILS_REQ", req_body);
        int ret = framework_http_post_json(LANG_GET_DETAILS_URL, req_body, NULL,
                                           prv_http_req_callback, NULL);
        cJSON_free(req_body);
        if (ret != 0)
        {
            OSI_LOGE(TAG, "[FETCH-META] Failed to queue POST request (%d)", ret);
            return -3;
        }
    }

    // Wait on semaphore for response
    bool acquired = osiSemaphoreTryAcquire(s_mgr.http_sem, LANG_HTTP_TIMEOUT_MS + 2000);
    if (!acquired)
    {
        OSI_LOGE(TAG, "[FETCH-META] HTTP request timed out (%d ms)", LANG_HTTP_TIMEOUT_MS);
        return -4;
    }

    if (s_mgr.http_result != HTTP_RESULT_SUCCESS || s_mgr.http_status_code != 200 || !s_mgr.http_response_data)
    {
        OSI_LOGE(TAG, "[FETCH-META] HTTP failed: res=%d, code=%d",
                 s_mgr.http_result, s_mgr.http_status_code);
        return -5;
    }

    OSI_LOGI(TAG, "[FETCH-META] Response received (%u bytes)", s_mgr.http_response_len);
    if (s_mgr.http_response_data && s_mgr.http_response_len > 0)
    {
        log_string_in_chunks_with_label(TAG, "LANG_DETAILS_RESP", s_mgr.http_response_data);
    }

    int parse_ret = prv_parse_metadata_json(s_mgr.http_response_data, req_id, out_meta);
    if (parse_ret != 0)
    {
        OSI_LOGE(TAG, "[FETCH-META] JSON parse validation failed (%d)", parse_ret);
        return -6;
    }

    return 0;
}

static int prv_resolve_zip_download(lang_zip_info_t *entry, const char *zip_path, char *out_download_url, size_t max_url_len)
{
    if (entry->url[0] != '\0')
    {
        strncpy(out_download_url, entry->url, max_url_len - 1);
        out_download_url[max_url_len - 1] = '\0';
        return 0;
    }

    // Production mode: Query DownloadLanguageFile API
    cJSON *req = cJSON_CreateObject();
    cJSON_AddNumberToObject(req, "size", entry->size);
    cJSON_AddStringToObject(req, "uuid", entry->uuid);
    cJSON_AddStringToObject(req, "sign", entry->sign);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    if (!body)
        return -1;

    OSI_LOGI(TAG, "[PROD-DL] Querying %s (length=%u)", LANG_DOWNLOAD_FILE_URL, (unsigned int)strlen(body));
    log_string_in_chunks_with_label(TAG, "LANG_DOWNLOAD_REQ", body);
    s_mgr.http_result = -1;
    s_mgr.http_status_code = 0;
    int ret = framework_http_post_json(LANG_DOWNLOAD_FILE_URL, body, NULL, prv_http_req_callback, NULL);
    cJSON_free(body);
    if (ret != 0)
    {
        OSI_LOGE(TAG, "[PROD-DL] Failed to post DownloadLanguageFile request (%d)", ret);
        return -2;
    }

    if (!osiSemaphoreTryAcquire(s_mgr.http_sem, LANG_HTTP_TIMEOUT_MS))
    {
        OSI_LOGE(TAG, "[PROD-DL] DownloadLanguageFile query timed out");
        return -3;
    }

    if (s_mgr.http_result != HTTP_RESULT_SUCCESS || s_mgr.http_status_code != 200 || !s_mgr.http_response_data)
    {
        OSI_LOGE(TAG, "[PROD-DL] DownloadLanguageFile query failed (code=%d)", s_mgr.http_status_code);
        return -4;
    }

    // Check if response is raw binary ZIP (starts with "PK")
    if (s_mgr.http_response_len >= 4 &&
        s_mgr.http_response_data[0] == 'P' &&
        s_mgr.http_response_data[1] == 'K')
    {
        OSI_LOGI(TAG, "[PROD-DL] Response is direct binary ZIP (%u bytes)", s_mgr.http_response_len);
        int fd = fs_open(zip_path, "wb+");
        if (fd <= 0)
            return -5;
        fs_write(s_mgr.http_response_data, 1, s_mgr.http_response_len, fd);
        fs_close(fd);
        out_download_url[0] = '\0'; // Direct file saved
        return 1;
    }

    // Parse JSON for URL
    cJSON *resp_json = cJSON_Parse(s_mgr.http_response_data);
    if (resp_json)
    {
        cJSON *j_url = cJSON_GetObjectItem(resp_json, "url");
        if (!j_url)
            j_url = cJSON_GetObjectItem(resp_json, "download_url");
        if (!j_url)
            j_url = cJSON_GetObjectItem(resp_json, "file_url");
        if (!j_url)
            j_url = cJSON_GetObjectItem(resp_json, "Data");
        if (j_url && cJSON_IsString(j_url) && j_url->valuestring && j_url->valuestring[0] != '\0')
        {
            strncpy(out_download_url, j_url->valuestring, max_url_len - 1);
            out_download_url[max_url_len - 1] = '\0';
            strncpy(entry->url, j_url->valuestring, sizeof(entry->url) - 1);
            cJSON_Delete(resp_json);
            return 0;
        }
        cJSON_Delete(resp_json);
    }

    OSI_LOGE(TAG, "[PROD-DL] Could not resolve download URL or binary ZIP from response");
    return -6;
}

#define LANG_DOWNLOAD_CHUNK_SIZE (32 * 1024)

static int prv_download_zip_direct(const char *url, const char *zip_path, uint32_t expected_size)
{
    if (!url || url[0] == '\0' || !zip_path)
    {
        return -1;
    }

    fs_remove(zip_path);
    int fd = fs_open(zip_path, "wb+");
    if (fd <= 0)
    {
        OSI_LOGE(TAG, "[DL-DIRECT] Failed to create destination file: %s (fd=%d)", zip_path, fd);
        return -2;
    }

    char *buf = (char *)malloc(LANG_DOWNLOAD_CHUNK_SIZE + 1);
    if (!buf)
    {
        OSI_LOGE(TAG, "[DL-DIRECT] Failed to allocate download buffer (%d bytes)", LANG_DOWNLOAD_CHUNK_SIZE);
        fs_close(fd);
        return -3;
    }

    char *hdr_buf = (char *)malloc(1024);
    if (!hdr_buf)
    {
        OSI_LOGE(TAG, "[DL-DIRECT] Failed to allocate header buffer");
        free(buf);
        fs_close(fd);
        return -3;
    }

    char current_url[256];
    strncpy(current_url, url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';

    uint32_t total_size = expected_size;
    uint32_t offset = 0;

    if (total_size == 0)
    {
        OSI_LOGI(TAG, "[DL-DIRECT] expected_size is 0, downloading directly: %s", current_url);
        httpclient_t client = {0};
        httpclient_data_t client_data = {0};
        client.verify_mode = 0;
        client_data.response_buf = buf;
        client_data.response_buf_len = LANG_DOWNLOAD_CHUNK_SIZE + 1;

        HTTPCLIENT_RESULT res = httpclient_get(&client, current_url, &client_data);
        if (res >= 0 && client.response_code == 200 && client_data.response_content_len > 0)
        {
            fs_write(buf, 1, client_data.response_content_len, fd);
            offset = client_data.response_content_len;
        }
        else
        {
            OSI_LOGE(TAG, "[DL-DIRECT] Direct GET failed: res=%d, code=%d", (int)res, client.response_code);
            fs_close(fd);
            free(hdr_buf);
            free(buf);
            fs_remove(zip_path);
            return -4;
        }
    }
    else
    {
        while (offset < total_size)
        {
            uint32_t chunk = total_size - offset;
            if (chunk > LANG_DOWNLOAD_CHUNK_SIZE)
            {
                chunk = LANG_DOWNLOAD_CHUNK_SIZE;
            }

            // Wait for cellular network and PDP context to be ready before starting chunk
            if (!framework_network_is_connected() || !framework_network_is_pdp_activated())
            {
                int net_wait = 0;
                while ((!framework_network_is_connected() || !framework_network_is_pdp_activated()) && net_wait < 15)
                {
                    OSI_LOGW(TAG, "[DL-DIRECT] Waiting for cellular PDP connection before offset %lu... (%d/15)",
                             (unsigned long)offset, net_wait + 1);
                    osiThreadSleep(2000);
                    net_wait++;
                }
            }

            char range_header[128];
            snprintf(range_header, sizeof(range_header),
                     "Range: bytes=%lu-%lu\r\nAccept: */*\r\nConnection: close\r\n",
                     (unsigned long)offset, (unsigned long)(offset + chunk - 1));

            bool chunk_success = false;
            for (int retry = 0; retry < 3; retry++)
            {
                if (retry > 0)
                {
                    OSI_LOGW(TAG, "[DL-DIRECT] Retrying chunk %lu-%lu (attempt %d/3)",
                             (unsigned long)offset, (unsigned long)(offset + chunk - 1), retry + 1);

                    // Wait for cellular/PDP reconnect if network glitched during download
                    int net_wait = 0;
                    while ((!framework_network_is_connected() || !framework_network_is_pdp_activated()) && net_wait < 15)
                    {
                        OSI_LOGW(TAG, "[DL-DIRECT] Cellular/PDP reconnecting, waiting... (%d/15)", net_wait + 1);
                        osiThreadSleep(2000);
                        net_wait++;
                    }
                    osiThreadSleep(1000);
                }

                httpclient_t client = {0};
                httpclient_data_t client_data = {0};
                memset(hdr_buf, 0, 1024);

                client.header = range_header;
                client.verify_mode = 0;
                client_data.response_buf = buf;
                client_data.response_buf_len = chunk + 1;
                client_data.header_buf = hdr_buf;
                client_data.header_buf_len = 1024;
                client_data.response_content_len = 0;

                HTTPCLIENT_RESULT res = httpclient_get(&client, current_url, &client_data);
                int resp_code = client.response_code;

                if ((resp_code == 301 || resp_code == 302 || resp_code == 307) && res >= 0)
                {
                    int loc_pos = 0, loc_len = 0;
                    if (httpclient_get_response_header_value(hdr_buf, "Location", &loc_pos, &loc_len) == 0 &&
                        loc_len > 0 && loc_len < (int)sizeof(current_url))
                    {
                        memcpy(current_url, hdr_buf + loc_pos, loc_len);
                        current_url[loc_len] = '\0';
                        OSI_LOGI(TAG, "[DL-DIRECT] Redirected (%d) -> %s", resp_code, current_url);
                        retry--;
                        continue;
                    }
                }

                if ((res == HTTPCLIENT_OK || res == HTTPCLIENT_RETRIEVE_MORE_DATA) &&
                    (resp_code == 206 || (resp_code == 200 && offset == 0)) &&
                    client_data.response_content_len > 0)
                {

                    while (audio_resource_is_playing())
                    {
                        osiThreadSleep(20);
                    }

                    size_t written = fs_write(buf, 1, client_data.response_content_len, fd);
                    if (written != (size_t)client_data.response_content_len)
                    {
                        OSI_LOGE(TAG, "[DL-DIRECT] Flash write error: wrote=%u, expected=%d",
                                 (unsigned)written, client_data.response_content_len);
                        break;
                    }

                    offset += client_data.response_content_len;
                    chunk_success = true;

                    int percent = (int)((offset * 100ULL) / total_size);
                    OSI_LOGI(TAG, "[DL-DIRECT] Downloaded %lu/%lu bytes (%d%%)",
                             (unsigned long)offset, (unsigned long)total_size, percent);
                    break;
                }
                else
                {
                    OSI_LOGW(TAG, "[DL-DIRECT] Chunk fetch failed: res=%d, code=%d, len=%d",
                             (int)res, resp_code, client_data.response_content_len);
                }
            }

            if (!chunk_success)
            {
                OSI_LOGE(TAG, "[DL-DIRECT] Exhausted retries for chunk at offset %lu", (unsigned long)offset);
                fs_close(fd);
                free(hdr_buf);
                free(buf);
                fs_remove(zip_path);
                return -5;
            }
        }
    }

    fs_sync(fd);
    fs_close(fd);
    free(hdr_buf);
    free(buf);

    OSI_LOGI(TAG, "[DL-DIRECT] Finished download: %lu bytes saved to %s", (unsigned long)offset, zip_path);
    return 0;
}

static int prv_download_and_verify_all_zips(lang_metadata_t *meta)
{
    fs_makedir(LANG_TEMP_DIR);
    prv_clean_dir(LANG_TEMP_DIR);

    for (int i = 0; i < meta->zip_count; i++)
    {
        lang_zip_info_t *entry = &meta->zip_files[i];
        char zip_path[128];
        snprintf(zip_path, sizeof(zip_path), "%s/pack_%d.zip", LANG_TEMP_DIR, i);

        bool verified = false;

        for (int attempt = 1; attempt <= LANG_MAX_RETRIES; attempt++)
        {
            OSI_LOGI(TAG, "[DL-ZIP] ZIP #%d (attempt %d/%d) url=%s -> %s",
                     i, attempt, LANG_MAX_RETRIES, entry->url, zip_path);

            fs_remove(zip_path);
            s_mgr.dl_result_msg_id = -1;

            char resolved_url[256] = {0};
            int res_ret = prv_resolve_zip_download(entry, zip_path, resolved_url, sizeof(resolved_url));
            if (res_ret < 0)
            {
                OSI_LOGE(TAG, "[DL-ZIP] Failed to resolve download for entry #%d (%d)", i, res_ret);
                osiThreadSleep(1000);
                continue;
            }

            if (res_ret == 0)
            {
                int dl_ret = prv_download_zip_direct(resolved_url, zip_path, entry->size);
                if (dl_ret != 0)
                {
                    OSI_LOGE(TAG, "[DL-ZIP] Direct download failed with err=%d", dl_ret);
                    fs_remove(zip_path);
                    osiThreadSleep(1000);
                    continue;
                }
            }
            else
            {
                OSI_LOGI(TAG, "[DL-ZIP] Direct binary ZIP saved to %s", zip_path);
            }
            uint32_t actual_size = fs_size(zip_path);
            if (actual_size != entry->size)
            {
                OSI_LOGE(TAG, "[DL-ZIP] Size mismatch: actual=%u, expected=%u",
                         actual_size, entry->size);
                fs_remove(zip_path);
                continue;
            }

            char actual_sha256[65] = {0};
            if (prv_calc_file_sha256(zip_path, actual_sha256) != 0)
            {
                OSI_LOGE(TAG, "[DL-ZIP] Failed to calculate SHA-256 for %s", zip_path);
                fs_remove(zip_path);
                continue;
            }

            if (!prv_sha256_matches(actual_sha256, entry->sign))
            {
                OSI_LOGE(TAG, "[DL-ZIP] SHA-256 mismatch! actual=%s, expected=%s",
                         actual_sha256, entry->sign);
                fs_remove(zip_path);
                continue;
            }

            OSI_LOGI(TAG, "[DL-ZIP] ZIP #%d verified successfully! (SHA-256: %s, Size: %u)",
                     i, actual_sha256, actual_size);
            verified = true;
            break;
        }

        if (!verified)
        {
            OSI_LOGE(TAG, "[DL-ZIP] Exhausted retries for ZIP #%d (uuid=%s)", i, entry->uuid);
            return -1;
        }
    }

    return 0;
}

static int prv_extract_all_zips(lang_metadata_t *meta)
{
    fs_makedir(LANG_STAGING_DIR);
    prv_clean_dir(LANG_STAGING_DIR);

    for (int i = 0; i < meta->zip_count; i++)
    {
        char zip_path[128];
        snprintf(zip_path, sizeof(zip_path), "%s/pack_%d.zip", LANG_TEMP_DIR, i);

        OSI_LOGI(TAG, "[UNZIP] Extracting %s -> %s/", zip_path, LANG_STAGING_DIR);
        s_mgr.unzip_result_msg_id = -1;

        int ret = framework_unzip_start(zip_path, LANG_STAGING_DIR, FRAMEWORK_UNZIP_TYPE_RESOURCES);
        if (ret != 0)
        {
            OSI_LOGE(TAG, "[UNZIP] Failed to start unzip (%d)", ret);
            return -1;
        }

        bool unzip_ok = osiSemaphoreTryAcquire(s_mgr.unzip_sem, LANG_UNZIP_TIMEOUT_MS);
        if (!unzip_ok)
        {
            OSI_LOGE(TAG, "[UNZIP] Unzip operation timed out (%d ms)", LANG_UNZIP_TIMEOUT_MS);
            return -2;
        }

        if (s_mgr.unzip_result_msg_id != MSG_UNZIP_SUCCESS)
        {
            OSI_LOGE(TAG, "[UNZIP] Unzip failed with msg_id=%d", s_mgr.unzip_result_msg_id);
            return -3;
        }

        OSI_LOGI(TAG, "[UNZIP] ZIP #%d extracted successfully", i);
    }

    int audio_count = prv_count_audio_files(LANG_STAGING_DIR);
    OSI_LOGI(TAG, "[UNZIP] Validated staging directory: %d audio files found", audio_count);
    if (audio_count <= 0)
    {
        OSI_LOGE(TAG, "[UNZIP] No valid .mp3 or .wav audio files extracted");
        return -4;
    }

    return 0;
}

static void prv_tms_lang_worker_task(void *param)
{
    (void)param;
    OSI_LOGI(TAG, "[TASK] TMS Language Download Worker task started");

    while (1)
    {
        osiSemaphoreAcquire(s_mgr.trigger_sem);

        prv_lock();
        char req_id[32] = {0};
        char url[256] = {0};
        strncpy(req_id, s_mgr.pending_req_id, sizeof(req_id) - 1);
        strncpy(url, s_mgr.pending_url, sizeof(url) - 1);
        prv_unlock();

        OSI_LOGI(TAG, "==================================================");
        OSI_LOGI(TAG, "[WORKFLOW] Starting TMS Language Download: req_id=%s, url=%s",
                 req_id, url[0] ? url : "(production details)");
        OSI_LOGI(TAG, "==================================================");

        static lang_metadata_t s_meta;
        memset(&s_meta, 0, sizeof(s_meta));
        lang_metadata_t *meta = &s_meta;

        char sn[32] = {0};
        framework_device_config_get_sn(sn, sizeof(sn));

        // 1. Fetch and Parse Metadata
        int ret = prv_fetch_metadata("https://klgbefvjgjtcwcsyfwwc.supabase.co/storage/v1/object/public/audio/language_response.txt", req_id, meta); // pv
        if (ret != 0)
        {
            OSI_LOGE(TAG, "[WORKFLOW] Metadata retrieval/parsing failed (%d)", ret);
            customer_send_language_ack(
                strtod(req_id, NULL),
                "",
                sn,
                "Failed",
                "Metadata retrieval/validation failed");
            goto cleanup_and_idle;
        }

        // 2. Sequentially Download and Verify All ZIP Files
        ret = prv_download_and_verify_all_zips(meta);
        if (ret != 0)
        {
            OSI_LOGE(TAG, "[WORKFLOW] ZIP download or verification failed (%d)", ret);
            for (int i = 0; i < meta->zip_count; i++)
            {
                customer_send_language_ack(
                    meta->request_id,
                    meta->zip_files[i].uuid,
                    meta->device_id,
                    "Failed",
                    "Download or SHA-256 verification failed");
            }
            goto cleanup_and_idle;
        }

        // 3. Extract All ZIP Archives into Staging
        ret = prv_extract_all_zips(meta);
        if (ret != 0)
        {
            OSI_LOGE(TAG, "[WORKFLOW] Extraction or audio validation failed (%d)", ret);
            for (int i = 0; i < meta->zip_count; i++)
            {
                customer_send_language_ack(
                    meta->request_id,
                    meta->zip_files[i].uuid,
                    meta->device_id,
                    "Failed",
                    "Extraction or audio validation failed");
            }
            goto cleanup_and_idle;
        }

        // 4. Safe Language Deployment & Activation
        soundbox_language_t new_lang = SOUNDBOX_DEFAULT_LANGUAGE;
        if (!soundbox_str_to_language(meta->newlanguage, &new_lang))
        {
            OSI_LOGW(TAG, "[WORKFLOW] Unrecognized newlanguage '%s', fallback to default enum", meta->newlanguage);
            new_lang = SOUNDBOX_DEFAULT_LANGUAGE;
        }

        const char *target_folder = soundbox_get_audio_folder(new_lang);
        if (!target_folder)
        {
            if (strcmp(meta->newlanguage, "1") == 0)
            {
                target_folder = HINDI_AUDIO_FOLDER;
            }
            else
            {
                target_folder = ENGLISH_AUDIO_FOLDER;
            }
        }

        OSI_LOGI(TAG, "[WORKFLOW] Deploying staging audio files to target folder: %s", target_folder);
        ret = prv_deploy_staging_to_target(LANG_STAGING_DIR, target_folder);
        if (ret != 0)
        {
            OSI_LOGE(TAG, "[WORKFLOW] Failed to deploy audio to %s (%d)", target_folder, ret);
            for (int i = 0; i < meta->zip_count; i++)
            {
                customer_send_language_ack(
                    meta->request_id,
                    meta->zip_files[i].uuid,
                    meta->device_id,
                    "Failed",
                    "Audio deployment to target folder failed");
            }
            goto cleanup_and_idle;
        }

        // 5. Update and Persist Active Language
        soundbox_set_language(new_lang, true);
        soundbox_language_t verify_lang = SOUNDBOX_DEFAULT_LANGUAGE;
        soundbox_load_language(&verify_lang);
        if (verify_lang != new_lang)
        {
            OSI_LOGE(TAG, "[WORKFLOW] Persistence check failed! expected=%d, loaded=%d",
                     (int)new_lang, (int)verify_lang);
        }
        else
        {
            OSI_LOGI(TAG, "[WORKFLOW] Language %s successfully persisted to %s",
                     soundbox_language_name(new_lang), TEMPLATE_LANGUAGE_FILE_PATH);
        }

        // 6. Send SUCCESS Acknowledgement for All Downloaded UUIDs asynchronously
        OSI_LOGI(TAG, "[WORKFLOW] Sending SUCCESS ACKs for all %d packs...",
                 meta->zip_count);
        for (int i = 0; i < meta->zip_count; i++)
        {
            OSI_LOGI(TAG, "[WORKFLOW] Sending ACK (%d/%d): uuid=%s, req_id=%.0f",
                     i + 1, meta->zip_count, meta->zip_files[i].uuid, meta->request_id);
            int ack_ret = customer_send_language_ack(
                meta->request_id,
                meta->zip_files[i].uuid,
                meta->device_id,
                "Success",
                "");
            if (ack_ret == 0)
            {
                OSI_LOGI(TAG, "[WORKFLOW] ACK (%d/%d) enqueued successfully!",
                         i + 1, meta->zip_count);
            }
            else
            {
                OSI_LOGW(TAG, "[WORKFLOW] ACK (%d/%d) enqueue failed with code %d",
                         i + 1, meta->zip_count, ack_ret);
            }
        }
        OSI_LOGI(TAG, "[WORKFLOW] All %d ACKs enqueued for TMS server!", meta->zip_count);
        if (meta->request_id > 0)
        {
            customer_send_change_language_ack(
                meta->request_id,
                meta->device_id,
                " Success ",
                "",
                soundbox_language_to_numeric_str(new_lang));
        }

        // 7. Optional upgrade success sound prompt
        if (fs_size("D:/FEMALE/upgrade-success.mp3") > 0 || fs_size("D:/upgrade-success.mp3") > 0)
        {
            audio_resource_play_combo_async_mid_priority("", "upgrade-success.mp3", NULL, 1);
            osiThreadSleep(1500);
        }

        OSI_LOGI(TAG, "[WORKFLOW] Complete TMS Language Pack update workflow finished successfully!");

        // Clean up temporary files before restart
        prv_clean_dir(LANG_TEMP_DIR);
        prv_clean_dir(LANG_STAGING_DIR);
        if (s_mgr.http_response_data)
        {
            free(s_mgr.http_response_data);
            s_mgr.http_response_data = NULL;
        }
        s_mgr.http_response_len = 0;

        prv_lock();
        s_mgr.busy = false;
        s_mgr.pending_req_id[0] = '\0';
        s_mgr.pending_url[0] = '\0';
        prv_unlock();

        // 8. Device Restart after successful extraction, deployment, and ACK
        OSI_LOGI(TAG, "==================================================");
        OSI_LOGI(TAG, "[WORKFLOW] All audio files extracted and ACK sent! Restarting device now...");
        OSI_LOGI(TAG, "==================================================");
        // osiThreadSleep(1000);
        // osiSysReset();
        continue;

    cleanup_and_idle:
        if (req_id[0] != '\0' && strtod(req_id, NULL) > 0)
        {
            customer_send_change_language_ack(
                strtod(req_id, NULL),
                sn,
                "Failed",
                "Language download workflow failed",
                soundbox_language_to_numeric_str(soundbox_get_language()));
        }
        prv_clean_dir(LANG_TEMP_DIR);
        prv_clean_dir(LANG_STAGING_DIR);
        if (s_mgr.http_response_data)
        {
            free(s_mgr.http_response_data);
            s_mgr.http_response_data = NULL;
        }
        s_mgr.http_response_len = 0;

        prv_lock();
        s_mgr.busy = false;
        s_mgr.pending_req_id[0] = '\0';
        s_mgr.pending_url[0] = '\0';
        prv_unlock();
    }
}

/* ============================================================================
 * Public APIs
 * ============================================================================ */

int customer_lang_download_init(void)
{
    if (s_mgr.initialized)
    {
        return 0;
    }

    s_mgr.mutex = osiMutexCreate();
    s_mgr.trigger_sem = osiSemaphoreCreate(1, 0);
    s_mgr.http_sem = osiSemaphoreCreate(1, 0);
    s_mgr.dl_sem = osiSemaphoreCreate(1, 0);
    s_mgr.unzip_sem = osiSemaphoreCreate(1, 0);

    s_mgr.worker_thread = osiThreadCreate(
        "tms_lang",
        prv_tms_lang_worker_task,
        NULL,
        120,
        24 * 1024);

    if (!s_mgr.worker_thread)
    {
        OSI_LOGE(TAG, "[INIT] Failed to create TMS language download worker thread");
        return -1;
    }

    s_mgr.initialized = true;
    OSI_LOGI(TAG, "[INIT] TMS Language Download Module initialized successfully");
    return 0;
}

bool customer_lang_download_is_busy(void)
{
    prv_lock();
    bool b = s_mgr.busy;
    prv_unlock();
    return b;
}

int customer_lang_download_trigger(const char *request_id_str, const char *metadata_url)
{
    if (!s_mgr.initialized)
    {
        customer_lang_download_init();
    }

    prv_lock();
    if (s_mgr.busy)
    {
        prv_unlock();
        OSI_LOGW(TAG, "[TRIGGER] Update already running, rejecting duplicate trigger (req_id=%s)",
                 request_id_str ? request_id_str : "(null)");
        return -1;
    }

    s_mgr.busy = true;
    if (request_id_str)
    {
        strncpy(s_mgr.pending_req_id, request_id_str, sizeof(s_mgr.pending_req_id) - 1);
    }
    else
    {
        s_mgr.pending_req_id[0] = '\0';
    }

    if (metadata_url)
    {
        strncpy(s_mgr.pending_url, metadata_url, sizeof(s_mgr.pending_url) - 1);
    }
    else
    {
        s_mgr.pending_url[0] = '\0';
    }

    prv_unlock();

    osiSemaphoreRelease(s_mgr.trigger_sem);
    return 0;
}

int customer_lang_download_handle_mqtt_command(const char *payload)
{
    if (!payload || payload[0] == '\0')
    {
        return -1;
    }

    OSI_LOGI(TAG, "[MQTT-CMD] Parsing command payload (length=%u)", (unsigned int)strlen(payload));
    log_string_in_chunks_with_label(TAG, "LANG_MQTT_CMD", payload);

    char *copy = (char *)malloc(strlen(payload) + 1);
    if (!copy)
    {
        OSI_LOGE(TAG, "[MQTT-CMD] Malloc failed for command parsing");
        return -2;
    }
    strcpy(copy, payload);

    // Expected tokens: DOWNLOADLANG|<request_id>|<url>
    char *tokens[3] = {0};
    int count = 0;
    char *p = copy;

    while (*p && count < 3)
    {
        tokens[count++] = p;
        char *pipe = strchr(p, '|');
        if (pipe)
        {
            *pipe = '\0';
            p = pipe + 1;
        }
        else
        {
            break;
        }
    }

    for (int i = 0; i < count; i++)
    {
        tokens[i] = prv_trim(tokens[i]);
    }

    if (count < 2 || strcmp(tokens[0], "DOWNLOADLANG") != 0)
    {
        OSI_LOGE(TAG, "[MQTT-CMD] Invalid command format. Expected 'DOWNLOADLANG|<request_id>[|<url>]'");
        free(copy);
        return -3;
    }

    const char *req_id = tokens[1];
    const char *url = (count >= 3 && tokens[2][0] != '\0') ? tokens[2] : NULL;

    OSI_LOGI(TAG, "[MQTT-CMD] Recognized DOWNLOADLANG: req_id=%s, url=%s",
             req_id, url ? url : "(none)");

    int ret = customer_lang_download_trigger(req_id, url);
    free(copy);
    return ret;
}
