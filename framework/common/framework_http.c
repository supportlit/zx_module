/**
 * @file framework_http.c
 * @brief Framework HTTP Client Module Implementation
 * @details Non-blocking HTTP request processing with dedicated task plus download worker
 */

#include "fs_api.h"
#include "framework_http.h"
#include "framework_log.h"
#include "framework_msg.h"
#include "httpclient.h"
#include "mbedtls/md5.h"
#include "mbedtls/base64.h"
#include "osi_api.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "framework_audio.h"

#define TAG "FRAMEWORK_HTTP"
#define HTTP_TASK_STACK_SIZE    (20 * 1024)
#define HTTP_TASK_PRIORITY      120
#define HTTP_REQUEST_QUEUE_SIZE 8
#define HTTP_DOWNLOAD_MAX_PATH  256

#define HTTP_DOWNLOAD_CHUNK_SIZE (8192*10)

/* ============================================================================
 * Download Helpers
 * ============================================================================ */

typedef struct {
    uint32_t downloaded;
    char partial_md5[33];
    char file_md5[33];
    char url[HTTP_MAX_URL_LEN];
} http_download_info_t;

static char g_http_download_msg_path[HTTP_DOWNLOAD_MAX_PATH];

static void prv_md5_bytes_to_hex(const uint8_t *md5_bytes, char *md5_out33)
{
    if (!md5_bytes || !md5_out33) {
        return;
    }
    for (int i = 0; i < 16; ++i) {
        sprintf(md5_out33 + i * 2, "%02x", md5_bytes[i]);
    }
    md5_out33[32] = '\0';
}

static int prv_parse_md5_from_header(const char *val, int val_len, char *md5_out33)
{
    if (!val || val_len <= 0 || !md5_out33) {
        return -1;
    }
    char tmp[128] = {0};
    int copy_len = (val_len < (int)sizeof(tmp) - 1) ? val_len : (int)sizeof(tmp) - 1;
    memcpy(tmp, val, copy_len);
    tmp[copy_len] = '\0';

    char *p = tmp;
    while (*p && isspace((unsigned char)*p)) p++;
    int len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) {
        p[len - 1] = '\0';
        len--;
    }

    if (len >= 2 && p[0] == '"' && p[len - 1] == '"') {
        p[len - 1] = '\0';
        p++;
        len -= 2;
    }
    if (len > 2 && p[0] == 'W' && p[1] == '/' && p[2] == '"') {
        p += 3;
        len -= 3;
        if (len > 0 && p[len - 1] == '"') {
            p[len - 1] = '\0';
            len--;
        }
    }

    int hex_ok = (len == 32);
    if (hex_ok) {
        for (int i = 0; i < 32; ++i) {
            char c = p[i];
            if (!isxdigit((unsigned char)c)) { hex_ok = 0; break; }
        }
    }
    if (hex_ok) {
        strncpy(md5_out33, p, 32);
        md5_out33[32] = '\0';
        return 0;
    }

    unsigned char md5_bytes[16] = {0};
    size_t out_len = 0;
    if (mbedtls_base64_decode(md5_bytes, sizeof(md5_bytes), &out_len, (const unsigned char *)p, len) == 0 && out_len == 16) {
        prv_md5_bytes_to_hex(md5_bytes, md5_out33);
        return 0;
    }
    return -1;
}

static int httpclient_get_content_info(const char *url, int *content_length, char *content_md5, size_t content_md5_len)
{
    if (!url || !content_length) {
        return -1;
    }
    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    char header_buf[HTTP_MAX_HEADER_LEN] = {0};
    client_data.header_buf = header_buf;
    client_data.header_buf_len = sizeof(header_buf);
    int ret = httpclient_head(&client, (char *)url, &client_data);
    if (ret != HTTPCLIENT_OK) {
        return -2;
    }
    int val_pos = 0;
    int val_len = 0;
    if (httpclient_get_response_header_value(header_buf, "Content-Length", &val_pos, &val_len) == 0) {
        char len_str[32] = {0};
        if (val_len > 0 && val_len < (int)sizeof(len_str)) {
            memcpy(len_str, header_buf + val_pos, val_len);
            len_str[val_len] = '\0';
            *content_length = atoi(len_str);
        } else {
            return -3;
        }
    } else {
        return -3;
    }

    if (content_md5 && content_md5_len > 0) {
        int md5_pos = 0;
        int md5_len = 0;
        if (httpclient_get_response_header_value(header_buf, "Content-MD5", &md5_pos, &md5_len) == 0 && md5_len > 0) {
            if (prv_parse_md5_from_header(header_buf + md5_pos, md5_len, content_md5) != 0) {
                content_md5[0] = '\0';
            }
        } else if (httpclient_get_response_header_value(header_buf, "ETag", &md5_pos, &md5_len) == 0 && md5_len > 0) {
            if (prv_parse_md5_from_header(header_buf + md5_pos, md5_len, content_md5) != 0) {
                content_md5[0] = '\0';
            }
        } else {
            content_md5[0] = '\0';
        }
    }
    return 0;
}

static int prv_load_download_info(const char *info_path, http_download_info_t *info)
{
    if (!info_path || !info) {
        return -1;
    }
    int fd = fs_open(info_path, "rb");
    if (fd <= 0) {
        return -2;
    }
    size_t n = fs_read(info, 1, sizeof(http_download_info_t), fd);
    fs_close(fd);
    return (n == sizeof(http_download_info_t)) ? 0 : -3;
}

static int prv_save_download_info(const char *info_path, const http_download_info_t *info)
{
    if (!info_path || !info) {
        return -1;
    }
    int fd = fs_open(info_path, "wb+");
    if (fd <= 0) {
        return -2;
    }
    while (audio_resource_is_playing())
    {
        osiThreadSleep(20);
    }
    
    size_t n = fs_write(info, 1, sizeof(http_download_info_t), fd);
    fs_sync(fd);
    fs_close(fd);
    return (n == sizeof(http_download_info_t)) ? 0 : -3;
}

static void prv_remove_download_info(const char *info_path)
{
    if (info_path) {
        fs_remove(info_path);
    }
}

OSI_UNUSED static int prv_calc_file_md5(const char *file_path, char *md5_out33)
{
    if (!file_path || !md5_out33) {
        return -1;
    }
    int fd = fs_open(file_path, "rb");
    if (fd <= 0) {
        return -2;
    }
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    uint8_t buf[4096];
    int n;
    while ((n = fs_read(buf, 1, sizeof(buf), fd)) > 0) {
        mbedtls_md5_update_ret(&ctx, buf, n);
    }
    fs_close(fd);
    uint8_t md5[16];
    mbedtls_md5_finish_ret(&ctx, md5);
    mbedtls_md5_free(&ctx);
    prv_md5_bytes_to_hex(md5, md5_out33);
    return 0;
}

static int prv_calc_partial_md5(const char *file_path, uint32_t length, char *md5_out33)
{
    if (!file_path || !md5_out33) {
        return -1;
    }
    int fd = fs_open(file_path, "rb");
    if (fd <= 0) {
        return -2;
    }
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    uint8_t buf[4096];
    uint32_t remaining = length;
    while (remaining > 0) {
        int to_read = (remaining > sizeof(buf)) ? sizeof(buf) : (int)remaining;
        int n = fs_read(buf, 1, to_read, fd);
        if (n <= 0) {
            fs_close(fd);
            mbedtls_md5_free(&ctx);
            return -3;
        }
        mbedtls_md5_update_ret(&ctx, buf, n);
        remaining -= (uint32_t)n;
    }
    fs_close(fd);
    uint8_t md5[16];
    mbedtls_md5_finish_ret(&ctx, md5);
    mbedtls_md5_free(&ctx);
    prv_md5_bytes_to_hex(md5, md5_out33);
    return 0;
}

static int prv_md5_update_range(mbedtls_md5_context *ctx, const char *file_path, uint32_t length)
{
    if (!ctx || !file_path) {
        return -1;
    }
    int fd = fs_open(file_path, "rb");
    if (fd <= 0) {
        return -2;
    }
    uint8_t buf[4096];
    uint32_t remaining = length;
    while (remaining > 0) {
        int to_read = (remaining > sizeof(buf)) ? sizeof(buf) : (int)remaining;
        int n = fs_read(buf, 1, to_read, fd);
        if (n <= 0) {
            fs_close(fd);
            return -3;
        }
        mbedtls_md5_update_ret(ctx, buf, n);
        remaining -= (uint32_t)n;
    }
    fs_close(fd);
    return 0;
}

static int prv_save_download_progress(const char *info_path, uint32_t downloaded, const char *file_md5, const char *url, mbedtls_md5_context *ctx)
{
    if (!info_path || !file_md5 || !url || !ctx) {
        return -1;
    }

    http_download_info_t info = {0};
    info.downloaded = downloaded;
    strncpy(info.file_md5, file_md5, sizeof(info.file_md5) - 1);
    strncpy(info.url, url, sizeof(info.url) - 1);

    mbedtls_md5_context snapshot;
    mbedtls_md5_init(&snapshot);
    mbedtls_md5_clone(&snapshot, ctx);
    uint8_t md5[16];
    mbedtls_md5_finish_ret(&snapshot, md5);
    mbedtls_md5_free(&snapshot);
    prv_md5_bytes_to_hex(md5, info.partial_md5);
    return prv_save_download_info(info_path, &info);
}

static int prv_md5_ieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    for (int i = 0; i < 32; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca == '\0' || cb == '\0') return 0;
        if (tolower((unsigned char)ca) != tolower((unsigned char)cb)) return 0;
    }
    return a[32] == '\0' && b[32] == '\0';
}

static void prv_send_download_msg(uint32_t msg_id, http_download_type_t type, int progress, int err, const char *file_path)
{
    framework_msg_t msg = {0};
    msg.id = msg_id;
    msg.param1 = type;
    msg.param2 = progress;
    if (file_path) {
        strncpy(g_http_download_msg_path, file_path, sizeof(g_http_download_msg_path) - 1);
        g_http_download_msg_path[sizeof(g_http_download_msg_path) - 1] = '\0';
        msg.param3 = (uint32_t)g_http_download_msg_path;
    }
    OSI_LOGI(TAG, "msg_id=0x%x path=%s type=%d progress=%d err=%d", msg_id, file_path ? file_path : "", type, progress, err);
    framework_msg_send(&msg);
}

static int prv_download_file_task(const char *url, const char *save_path, http_download_type_t type, const char *expected_md5, void *arg, http_download_callback_t callback)
{
    if (!url || !save_path) {
        return -1;
    }

    char info_path[128];
    snprintf(info_path, sizeof(info_path), "%s.info", save_path);

    http_download_info_t info = {0};
    int has_info = (prv_load_download_info(info_path, &info) == 0);

    int total_size = 0;
    char remote_md5[33] = {0};
    if (httpclient_get_content_info(url, &total_size, remote_md5, sizeof(remote_md5)) != 0 || total_size <= 0) {
        prv_send_download_msg(MSG_HTTP_DOWNLOAD_FAIL, type, 0, -4, save_path);
        if(callback){callback(MSG_HTTP_DOWNLOAD_FAIL, save_path, arg);} 
        return -4;
    }

    char desired_md5[33] = {0};
    if (expected_md5 && strlen(expected_md5) == 32) {
        strncpy(desired_md5, expected_md5, sizeof(desired_md5) - 1);
    } else if (remote_md5[0]) {
        strncpy(desired_md5, remote_md5, sizeof(desired_md5) - 1);
    }

    int resume = 0;
    int offset = 0;

    /*
     * HTTP Resume Logic / 断点续传校验逻辑:
     * 1. Load download info / 读取进度文件
     * 2. Compare URL / 比较 URL
     * 3. Compare File MD5 (Expected or Remote) / 比较文件完整 MD5 (传入的 expected 或服务器返回的)
     * 4. Verify downloaded partial MD5 / 校验已下载部分的 MD5
     */
    if (has_info && info.downloaded > 0 && info.downloaded < (uint32_t)total_size && desired_md5[0]) {
        if (strncmp(info.url, url, sizeof(info.url)) == 0 && prv_md5_ieq(info.file_md5, desired_md5)) {
            char partial_md5[33] = {0};
            if (prv_calc_partial_md5(save_path, info.downloaded, partial_md5) == 0 && prv_md5_ieq(partial_md5, info.partial_md5)) {
                resume = 1;
                offset = info.downloaded;
                OSI_LOGI(TAG, "Resume accepted: partial MD5 match offset: %u", offset);
            } else {
                OSI_LOGI(TAG, "Resume rejected: partial MD5 mismatch");
            }
        } else {
            OSI_LOGI(TAG, "Resume rejected: URL or file MD5 changed");
        }
    }
    if (!resume) {
        prv_remove_download_info(info_path);
        memset(&info, 0, sizeof(info));
    }

    int fd = fs_open(save_path, resume ? "rb+" : "wb+");
    if (fd <= 0) {
        resume = 0;
        fs_remove(info_path);
        memset(&info, 0, sizeof(info));
        fd = fs_open(save_path, "wb+");
        if (fd < 0)
        {
            prv_send_download_msg(MSG_HTTP_DOWNLOAD_FAIL, type, 0, -3, save_path);
            if(callback){callback(MSG_HTTP_DOWNLOAD_FAIL, save_path, arg);}
            return -3;
        }
    }

    if (resume) {
        offset = info.downloaded;
        fs_seek(fd, offset, SEEK_SET);
    }

    mbedtls_md5_context md5_ctx;
    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts_ret(&md5_ctx);
    if (resume && offset > 0) {
        if (prv_md5_update_range(&md5_ctx, save_path, offset) != 0) {
            OSI_LOGI(TAG, "Resume rejected: failed to seed MD5 context");
            resume = 0;
            offset = 0;
            prv_remove_download_info(info_path);
            fs_close(fd);
            fd = fs_open(save_path, "wb+");
            mbedtls_md5_free(&md5_ctx);
            mbedtls_md5_init(&md5_ctx);
            mbedtls_md5_starts_ret(&md5_ctx);
        }
    }

    char *buf = malloc(HTTP_DOWNLOAD_CHUNK_SIZE + 1);
    if (!buf) {
        fs_close(fd);
        mbedtls_md5_free(&md5_ctx);
        prv_send_download_msg(MSG_HTTP_DOWNLOAD_FAIL, type, (offset * 100) / total_size, -2, save_path);
        if(callback){callback(MSG_HTTP_DOWNLOAD_FAIL, save_path, arg);}
        return -2;
    }

    prv_send_download_msg(MSG_HTTP_DOWNLOAD_START, type, (offset * 100) / total_size, 0, save_path);

    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    int last_percent = -1;

    while (offset < total_size) {
        int chunk = (total_size - offset > HTTP_DOWNLOAD_CHUNK_SIZE) ? HTTP_DOWNLOAD_CHUNK_SIZE : (total_size - offset);
        char range_header[64];
        snprintf(range_header, sizeof(range_header), "Range: bytes=%d-%d\r\n", offset, offset + chunk - 1);
        OSI_LOGI(0, "requesting range %s", range_header);

        int chunk_success = 0;
        int max_retries = 3;
        int last_http_ret = -1;
        int last_resp_code = 0;

        for (int retry = 0; retry < max_retries; retry++) {
            if (retry > 0) {
                OSI_LOGI(TAG, "Retrying chunk at offset %d (attempt %d/%d)...", offset, retry + 1, max_retries);
                osiThreadSleep(1000);
            }

            memset(&client, 0, sizeof(client));
            memset(&client_data, 0, sizeof(client_data));
            client.header = range_header;
            client_data.response_buf = buf;
            client_data.response_buf_len = chunk + 1;
            client_data.response_content_len = 0;

            int http_ret = httpclient_get(&client, (char *)url, &client_data);
            int resp_code = client.response_code;
            last_http_ret = http_ret;
            last_resp_code = resp_code;

            if (http_ret >= 0 && client_data.response_content_len > 0) {
                if (resp_code == 200 && offset > 0) {
                    OSI_LOGE(TAG, "FATAL: Server returned 200 OK instead of 206 Partial Content for offset %d! Server does not support HTTP Range.", offset);
                    last_http_ret = -10;
                    break;
                }
                chunk_success = 1;
                break;
            }

            OSI_LOGE(TAG, "Chunk at offset %d attempt %d/%d failed: ret=%d, resp_code=%d, len=%d",
                     offset, retry + 1, max_retries, http_ret, resp_code, client_data.response_content_len);
        }

        if (!chunk_success) {
            OSI_LOGE(TAG, "Chunk at offset %d failed after %d attempts (ret=%d, resp_code=%d)",
                     offset, max_retries, last_http_ret, last_resp_code);
            fs_close(fd);
            free(buf);
            mbedtls_md5_free(&md5_ctx);
            prv_send_download_msg(MSG_HTTP_DOWNLOAD_FAIL, type, (offset * 100) / total_size, -5, save_path);
            if(callback){callback(MSG_HTTP_DOWNLOAD_FAIL, save_path, arg);}
            return -5;
        }

        OSI_LOGI(0, "received %d bytes (code=%d)", client_data.response_content_len, client.response_code);

        while (audio_resource_is_playing())
        {
            osiThreadSleep(20);
        }
        size_t written = fs_write(buf, 1, client_data.response_content_len, fd);
        if (written != client_data.response_content_len) {
            fs_close(fd);
            free(buf);
            mbedtls_md5_free(&md5_ctx);
            prv_send_download_msg(MSG_HTTP_DOWNLOAD_FAIL, type, (offset * 100) / total_size, -6, save_path);
            if(callback){callback(MSG_HTTP_DOWNLOAD_FAIL, save_path, arg);}
            return -6;
        }
        fs_sync(fd);

        offset += client_data.response_content_len;
        mbedtls_md5_update_ret(&md5_ctx, (unsigned char *)buf, written);
        if (desired_md5[0]) {
            prv_save_download_progress(info_path, offset, desired_md5, url, &md5_ctx);
        }

        int percent = (offset * 100) / total_size;
        if (percent != last_percent) {
            prv_send_download_msg(MSG_HTTP_DOWNLOAD_PROGRESS, type, percent, 0, save_path);
            last_percent = percent;
        }
    }

    fs_close(fd);
    free(buf);

    if (desired_md5[0]) {
        char actual_md5[33] = {0};
        mbedtls_md5_context final_ctx;
        mbedtls_md5_init(&final_ctx);
        mbedtls_md5_clone(&final_ctx, &md5_ctx);
        uint8_t digest[16];
        mbedtls_md5_finish_ret(&final_ctx, digest);
        mbedtls_md5_free(&final_ctx);
        prv_md5_bytes_to_hex(digest, actual_md5);
        if (!prv_md5_ieq(actual_md5, desired_md5)) {
            mbedtls_md5_free(&md5_ctx);
            OSI_LOGI(TAG, "MD5 mismatch: expected=%s actual=%s", desired_md5, actual_md5);
            prv_remove_download_info(info_path);
            prv_send_download_msg(MSG_HTTP_DOWNLOAD_FAIL, type, 100, -7, save_path);
            if(callback){callback(MSG_HTTP_DOWNLOAD_FAIL, save_path, arg);} 
            return -7;
        }
    }

    mbedtls_md5_free(&md5_ctx);

    prv_remove_download_info(info_path);
    prv_send_download_msg(MSG_HTTP_DOWNLOAD_SUCCESS, type, 100, 0, save_path);
    if(callback){callback(MSG_HTTP_DOWNLOAD_SUCCESS, save_path, arg);}
    return 0;
}

/* ============================================================================
 * HTTP Request Infrastructure
 * ============================================================================ */

typedef enum {
    HTTP_JOB_TYPE_REQUEST = 0,
    HTTP_JOB_TYPE_DOWNLOAD
} http_job_type_t;

typedef struct {
    http_job_type_t job_type;
    union {
        struct {
            http_request_t request;
            http_callback_t callback;
        } http_req;
        struct {
            char url[HTTP_MAX_URL_LEN];
            char save_path[HTTP_DOWNLOAD_MAX_PATH];
            http_download_type_t type;
            char expected_md5[33];
            void *user_data;                        /* User data passed to callback */
            http_download_callback_t download_callback;
        } download;
    } payload;
} http_request_item_t;

typedef struct {
    osiThread_t *task_handle;
    osiMessageQueue_t *request_queue;
    uint8_t initialized;
} http_context_t;

static http_context_t g_http_ctx = {0};

static void prv_execute_http_job(http_request_item_t *item)
{
    if (!item) {
        OSI_LOGE(TAG , " Invalid job item");
        return;
    }

    if (item->job_type == HTTP_JOB_TYPE_DOWNLOAD) {
        OSI_LOGI(TAG , " Executing HTTP download: %s -> %s",
                 item->payload.download.url,
                 item->payload.download.save_path);
        prv_download_file_task(item->payload.download.url,
                       item->payload.download.save_path,
                       item->payload.download.type,
                       item->payload.download.expected_md5,
                       item->payload.download.user_data,
                       item->payload.download.download_callback);
        return;
    }

    http_request_t *request = &item->payload.http_req.request;
    http_callback_t callback = item->payload.http_req.callback;
    if (!callback) {
        OSI_LOGE(TAG , " Invalid request callback");
        return;
    }

    http_response_t response = {0};
    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    HTTPCLIENT_RESULT http_ret;
    char *response_buf = NULL;
    char *header_buf = NULL;

    OSI_LOGI(TAG , " Executing HTTP request: %s", request->url);
    if (request->body && (request->body_len > 0 || strlen(request->body) > 0)) {
        size_t b_len = request->body_len > 0 ? request->body_len : strlen(request->body);
        log_buffer_in_chunks_with_label(TAG, "HTTP_REQ", request->body, b_len);
    }

    response_buf = malloc(HTTP_RESPONSE_BUF_SIZE);
    if (!response_buf) {
        OSI_LOGE(TAG , " Failed to allocate response buffer");
        response.result = HTTP_RESULT_ERROR_MEMORY;
        callback(&response, request->user_data);
        return;
    }
    memset(response_buf, 0, HTTP_RESPONSE_BUF_SIZE);

    header_buf = malloc(HTTP_MAX_HEADER_LEN);
    if (!header_buf) {
        OSI_LOGE(TAG , " Failed to allocate header buffer");
        response.result = HTTP_RESULT_ERROR_MEMORY;
        callback(&response, request->user_data);
        free(response_buf);
        return;
    }
    memset(header_buf, 0, HTTP_MAX_HEADER_LEN);

    if (strlen(request->header) > 0) {
        strncpy(header_buf, request->header, HTTP_MAX_HEADER_LEN - 1);
    }

    client_data.response_buf = response_buf;
    client_data.response_buf_len = HTTP_RESPONSE_BUF_SIZE;
    client.header = header_buf;
    client.sni_host = request->sni_host;

    switch (request->method) {
        case HTTP_METHOD_GET:
            http_ret = httpclient_get(&client, request->url, &client_data);
            break;
        case HTTP_METHOD_POST:
            client_data.post_buf = request->body;
            client_data.post_buf_len = request->body_len;
            http_ret = httpclient_post(&client, request->url, &client_data);
            break;
        case HTTP_METHOD_PUT:
            client_data.post_buf = request->body;
            client_data.post_buf_len = request->body_len;
            http_ret = httpclient_put(&client, request->url, &client_data);
            break;
        case HTTP_METHOD_DELETE:
            http_ret = httpclient_delete(&client, request->url, &client_data);
            break;
        default:
            OSI_LOGE(TAG , " Unsupported HTTP method: %d", request->method);
            response.result = HTTP_RESULT_ERROR_INVALID_PARAM;
            callback(&response, request->user_data);
            free(response_buf);
            free(header_buf);
            return;
    }

    response.status_code = client.response_code;
    if (http_ret == HTTPCLIENT_OK && client.response_code >= 200 && client.response_code < 300) {
        response.result = HTTP_RESULT_SUCCESS;
        response.response_data = response_buf;
        response.response_len = strlen(response_buf);
        OSI_LOGI(TAG , " HTTP request success, status=%d, len=%u",
                 response.status_code, response.response_len);
        if (response.response_len > 0) {
            log_string_in_chunks_with_label(TAG, "HTTP_RESP", response_buf);
        }
    } else {
        response.result = HTTP_RESULT_ERROR_HTTP;
        response.response_data = response_buf;
        response.response_len = strlen(response_buf);
        OSI_LOGE(TAG , " HTTP request failed, ret=%d, status=%d",
                 http_ret, client.response_code);
        if (response.response_len > 0) {
            log_string_in_chunks_with_label(TAG, "HTTP_RESP", response_buf);
        }
    }

    callback(&response, request->user_data);

    if (request->body) {
        free(request->body);
    }
    free(response_buf);
    free(header_buf);
}

static void prv_http_task(void *param)
{
    (void)param;
    OSI_LOGI(TAG , " HTTP task started");

    http_request_item_t *item;
    while (1) {
        if (osiMessageQueueGet(g_http_ctx.request_queue, &item)) {
            if (item) {
                prv_execute_http_job(item);
                free(item);
            }
        }
    }
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

int framework_http_init(void)
{
    if (g_http_ctx.initialized) {
        OSI_LOGW(0, TAG ": HTTP module already initialized");
        return 0;
    }

    OSI_LOGI(TAG , " Initializing HTTP module");

    g_http_ctx.request_queue = osiMessageQueueCreate(HTTP_REQUEST_QUEUE_SIZE,
                                                     sizeof(http_request_item_t*));
    if (!g_http_ctx.request_queue) {
        OSI_LOGE(TAG , " Failed to create request queue");
        return -1;
    }

    g_http_ctx.task_handle = osiThreadCreate("http_task",
                                             prv_http_task,
                                             NULL,
                                             HTTP_TASK_PRIORITY,
                                             HTTP_TASK_STACK_SIZE);
    if (!g_http_ctx.task_handle) {
        OSI_LOGE(TAG , " Failed to create HTTP task");
        osiMessageQueueDelete(g_http_ctx.request_queue);
        g_http_ctx.request_queue = NULL;
        return -1;
    }

    g_http_ctx.initialized = 1;
    OSI_LOGI(TAG , " HTTP module initialized successfully");
    return 0;
}

void framework_http_deinit(void)
{
    if (!g_http_ctx.initialized) {
        return;
    }

    OSI_LOGI(TAG , " Deinitializing HTTP module");

    if (g_http_ctx.task_handle) {
        osiThreadExit();
        g_http_ctx.task_handle = NULL;
    }

    if (g_http_ctx.request_queue) {
        osiMessageQueueDelete(g_http_ctx.request_queue);
        g_http_ctx.request_queue = NULL;
    }

    g_http_ctx.initialized = 0;
    OSI_LOGI(TAG , " HTTP module deinitialized");
}

int framework_http_download_file_with_md5(const char *url, const char *save_path, http_download_type_t type, const char *expected_md5)
{
    if (!g_http_ctx.initialized) {
        OSI_LOGE(TAG , " HTTP module not initialized");
        return -1;
    }
    if (!url || !save_path) {
        OSI_LOGE(TAG , " Invalid download parameters");
        return -2;
    }

    http_request_item_t *item = malloc(sizeof(http_request_item_t));
    if (!item) {
        OSI_LOGE(TAG , " Failed to allocate download job");
        return -3;
    }
    memset(item, 0, sizeof(http_request_item_t));

    item->job_type = HTTP_JOB_TYPE_DOWNLOAD;
    strncpy(item->payload.download.url, url, HTTP_MAX_URL_LEN - 1);
    strncpy(item->payload.download.save_path, save_path, HTTP_DOWNLOAD_MAX_PATH - 1);
    item->payload.download.type = type;
    if (expected_md5) {
        strncpy(item->payload.download.expected_md5, expected_md5, sizeof(item->payload.download.expected_md5) - 1);
    }

    if (!osiMessageQueuePut(g_http_ctx.request_queue, &item)) {
        OSI_LOGE(TAG , " Failed to queue download job");
        free(item);
        return -4;
    }

    OSI_LOGI(TAG , " HTTP download queued: %s -> %s", url, save_path);
    return 0;
}

int framework_http_download_file(const char *url, const char *save_path, http_download_type_t type)
{
    return framework_http_download_file_with_md5(url, save_path, type, NULL);
}

int framework_http_download_file_callback_with_md5(const char *url, const char *save_path, http_download_type_t type, const char *expected_md5, http_download_callback_t callback, void *user_data)
{
    if (!g_http_ctx.initialized) {
        OSI_LOGE(TAG , " HTTP module not initialized");
        return -1;
    }
    if (!url || !save_path) {
        OSI_LOGE(TAG , " Invalid download parameters");
        return -2;
    }

    http_request_item_t *item = malloc(sizeof(http_request_item_t));
    if (!item) {
        OSI_LOGE(TAG , " Failed to allocate download job");
        return -3;
    }
    memset(item, 0, sizeof(http_request_item_t));

    item->job_type = HTTP_JOB_TYPE_DOWNLOAD;
    strncpy(item->payload.download.url, url, HTTP_MAX_URL_LEN - 1);
    strncpy(item->payload.download.save_path, save_path, HTTP_DOWNLOAD_MAX_PATH - 1);
    item->payload.download.type = type;
    if (expected_md5) {
        strncpy(item->payload.download.expected_md5, expected_md5, sizeof(item->payload.download.expected_md5) - 1);
    }
    item->payload.download.download_callback = callback;
    item->payload.download.user_data = user_data;

    if (!osiMessageQueuePut(g_http_ctx.request_queue, &item)) {
        OSI_LOGE(TAG , " Failed to queue download job");
        free(item);
        return -4;
    }

    OSI_LOGI(TAG , " HTTP download queued: %s -> %s", url, save_path);
    return 0;
}

int framework_http_download_file_callback(const char *url, const char *save_path, http_download_type_t type, http_download_callback_t callback, void *user_data)
{
    return framework_http_download_file_callback_with_md5(url, save_path, type, NULL, callback, user_data);
}

int framework_http_request_async(http_request_t *request, http_callback_t callback)
{
    if (!g_http_ctx.initialized) {
        OSI_LOGE(TAG , " HTTP module not initialized");
        return -1;
    }
    if (!request || !callback) {
        OSI_LOGE(TAG , " Invalid parameters");
        return -2;
    }
    if (strlen(request->url) == 0) {
        OSI_LOGE(TAG , " Empty URL");
        return -3;
    }

    http_request_item_t *item = malloc(sizeof(http_request_item_t));
    if (!item) {
        OSI_LOGE(TAG , " Failed to allocate request item");
        return -4;
    }
    memset(item, 0, sizeof(http_request_item_t));

    item->job_type = HTTP_JOB_TYPE_REQUEST;
    memcpy(&item->payload.http_req.request, request, sizeof(http_request_t));
    item->payload.http_req.callback = callback;

    /* Deep copy body to avoid stack/static lifetime issues */
    item->payload.http_req.request.body = NULL;
    item->payload.http_req.request.body_len = 0;
    size_t body_len = request->body_len;
    if ((!body_len) && request->body) {
        body_len = strlen(request->body);
    }
    if (request->body && body_len > 0) {
        char *dup = malloc(body_len + 1);
        if (!dup) {
            OSI_LOGE(TAG , " Failed to dup body");
            free(item);
            return -6;
        }
        memcpy(dup, request->body, body_len);
        dup[body_len] = '\0';
        item->payload.http_req.request.body = dup;
        item->payload.http_req.request.body_len = body_len;
    }

    if (!osiMessageQueuePut(g_http_ctx.request_queue, &item)) {
        OSI_LOGE(TAG , " Failed to queue request");
        if (item->payload.http_req.request.body) {
            free(item->payload.http_req.request.body);
        }
        free(item);
        return -5;
    }

    OSI_LOGI(TAG , " HTTP request queued: %s", request->url);
    return 0;
}

int framework_http_post_json(const char *url, const char *body, const char *header,
                             http_callback_t callback, void *user_data)
{
    if (!url || !body || !callback) {
        return -1;
    }

    http_request_t *request = (http_request_t *)malloc(sizeof(http_request_t));
    if (!request) {
        OSI_LOGE(TAG, "Failed to allocate http request memory");
        return -1;
    }
    memset(request, 0, sizeof(http_request_t));
    request->method = HTTP_METHOD_POST;
    request->timeout_ms = 10000;
    request->user_data = user_data;

    strncpy(request->url, url, HTTP_MAX_URL_LEN - 1);
    request->body = (char *)body;
    request->body_len = strlen(body);

    if (header && strlen(header) > 0) {
        strncpy(request->header, header, HTTP_MAX_HEADER_LEN - 1);
    } else {
        snprintf(request->header, HTTP_MAX_HEADER_LEN, "Content-Type: application/json\r\n");
    }

    int ret = framework_http_request_async(request, callback);
    free(request);
    return ret;
}

int framework_http_post_json_with_sni(const char *url, const char *body, const char *header,
                             http_callback_t callback, void *user_data, char *sni_host){
    if (!url || !body || !callback) {
        return -1;
    }

    http_request_t *request = (http_request_t *)malloc(sizeof(http_request_t));
    if (!request) {
        OSI_LOGE(TAG, "Failed to allocate http request memory");
        return -1;
    }
    memset(request, 0, sizeof(http_request_t));
    request->method = HTTP_METHOD_POST;
    request->timeout_ms = 10000;
    request->user_data = user_data;
    request->sni_host = sni_host;

    strncpy(request->url, url, HTTP_MAX_URL_LEN - 1);
    request->body = (char *)body;
    request->body_len = strlen(body);

    if (header && strlen(header) > 0) {
        strncpy(request->header, header, HTTP_MAX_HEADER_LEN - 1);
    } else {
        snprintf(request->header, HTTP_MAX_HEADER_LEN, "Content-Type: application/json\r\n");
    }

    int ret = framework_http_request_async(request, callback);
    free(request);
    return ret;
}
