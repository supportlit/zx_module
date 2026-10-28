/**

 * @file TmsExceptionDownload.c

 * @brief TMS Exception Audio & File Download Workflow (Async HTTP Queue Mode) and UPDATESBFLAG ACK

 */

#include "TmsExceptionDownload.h"

#include "customer_audio_download.h"

#include "customer_mqtt_handler.h"

#include "customer_config.h"

#include "customer_register.h"

#include "framework_http.h"

#include "httpclient.h"

#include "cJSON.h"

#include "framework_log.h"

#include "framework_device_config.h"

#include "fs_api.h"

#include "osi_api.h"

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <ctype.h>

#include <stdbool.h>

#define TAG "TMS_EXC"

#define TMS_HTTP_HDR_BUF_SIZE 2048

#define TMS_ACK_RESP_BUF_SIZE 1024

typedef struct
{

    char default_lang[16];

    char manifest_url[256];

} tms_exception_async_ctx_t;

static osiSemaphore_t *s_exception_sync_sem = NULL;
static tms_exception_download_status_t s_exception_sync_result = TMS_EXCEPTION_DOWNLOAD_FAILED;
static tms_exception_completion_cb_t s_exception_user_completion_cb = NULL;
static void *s_exception_user_completion_data = NULL;

void tms_exception_download_set_completion_callback(tms_exception_completion_cb_t cb, void *user_data)
{
    s_exception_user_completion_cb = cb;
    s_exception_user_completion_data = user_data;
}

static void prv_notify_exception_completion(tms_exception_download_status_t status)
{
    if (s_exception_sync_sem) {
        s_exception_sync_result = status;
        osiSemaphoreRelease(s_exception_sync_sem);
    }
    if (s_exception_user_completion_cb) {
        tms_exception_completion_cb_t cb = s_exception_user_completion_cb;
        void *ud = s_exception_user_completion_data;
        s_exception_user_completion_cb = NULL;
        s_exception_user_completion_data = NULL;
        cb(status, ud);
    }
}

static void prv_exception_batch_completion_cb(uint32_t batch_id, int success_count, int failed_count, int total_count, void *user_data)
{
    (void)batch_id;
    (void)user_data;
    OSI_LOGI(TAG, "[SYNC-EXC] Batch completion callback: success=%d, failed=%d, total=%d",
             success_count, failed_count, total_count);
    tms_exception_download_status_t st = (total_count > 0 && failed_count == 0 && success_count == total_count)
                                          ? TMS_EXCEPTION_DOWNLOAD_SUCCESS
                                          : TMS_EXCEPTION_DOWNLOAD_FAILED;
    prv_notify_exception_completion(st);
}

static void trim_whitespace(char *str)

{

    if (str == NULL)
    {

        return;
    }

    char *start = str;

    while (*start != '\0' && isspace((unsigned char)*start))
    {

        start++;
    }

    char *end = start + strlen(start);

    while (end > start && isspace((unsigned char)*(end - 1)))
    {

        end--;
    }

    *end = '\0';

    if (start != str)
    {

        memmove(str, start, (size_t)(end - start + 1));
    }
}

/**

 * @brief Extract the GEN.txt URL from the initial manifest body

 */

static int extract_gen_url_from_manifest(const char *manifest_body, char *out_url, size_t max_len)

{

    if (!manifest_body || !out_url || max_len == 0)
    {

        return -1;
    }

    const char *url_start = strstr(manifest_body, "http://");

    if (!url_start)
    {

        url_start = strstr(manifest_body, "https://");
    }

    if (!url_start)
    {

        OSI_LOGE(TAG, "No valid HTTP(S) URL found in initial manifest response");

        return -2;
    }

    size_t idx = 0;

    while (url_start[idx] != '\0' &&

           url_start[idx] != '\r' &&

           url_start[idx] != '\n' &&

           url_start[idx] != ' ' &&

           url_start[idx] != '\t' &&

           url_start[idx] != '|' &&

           idx < (max_len - 1))

    {

        out_url[idx] = url_start[idx];

        idx++;
    }

    out_url[idx] = '\0';

    trim_whitespace(out_url);

    if (out_url[0] == '\0')
    {

        OSI_LOGE(TAG, "Extracted empty GEN.txt URL from manifest");

        return -3;
    }

    return 0;
}

const char *tms_map_ack_status(bool requested, bool success)

{

    if (!requested)
    {

        return TMS_ACK_FLAG_SKIPPED;
    }

    return success ? TMS_ACK_FLAG_SUCCESS : TMS_ACK_FLAG_FAILED;
}

static int count_valid_manifest_entries(const char *manifest_text, bool *out_has_malformed)

{

    if (!manifest_text)
    {

        return 0;
    }

    int count = 0;

    bool malformed = false;

    const char *p = manifest_text;

    while (*p != '\0')
    {

        while (*p == '\r' || *p == '\n')
        {

            p++;
        }

        if (*p == '\0')
        {

            break;
        }

        const char *line_start = p;

        while (*p != '\0' && *p != '\r' && *p != '\n')
        {

            p++;
        }

        const char *pipe = NULL;

        bool non_space = false;

        for (const char *s = line_start; s < p; s++)
        {

            if (*s == '|')
            {

                pipe = s;
            }
            else if (!isspace((unsigned char)*s))
            {

                non_space = true;
            }
        }

        if (!non_space)
        {

            continue;
        }

        if (!pipe)
        {

            malformed = true;

            continue;
        }

        bool url_has_chars = false;

        for (const char *s = line_start; s < pipe; s++)
        {

            if (!isspace((unsigned char)*s))
            {

                url_has_chars = true;

                break;
            }
        }

        bool dst_has_chars = false;

        for (const char *s = pipe + 1; s < p; s++)
        {

            if (!isspace((unsigned char)*s))
            {

                dst_has_chars = true;

                break;
            }
        }

        if (url_has_chars && dst_has_chars)
        {

            count++;
        }
        else
        {

            malformed = true;
        }
    }

    if (out_has_malformed)
    {

        *out_has_malformed = malformed;
    }

    return count;
}

/**

 * @brief Parse GEN.txt manifest content line-by-line and queue audio downloads

 */

static void prv_parse_and_queue_gen_manifest(const char *manifest_text, const char *default_lang)

{

    if (!manifest_text || manifest_text[0] == '\0')
    {

        OSI_LOGE(TAG, "[ASYNC-GEN] Manifest text is empty or NULL");

        return;
    }

    size_t text_len = strlen(manifest_text);

    char *gen_buf = (char *)malloc(text_len + 1);

    if (!gen_buf)
    {

        OSI_LOGE(TAG, "[ASYNC-GEN] Failed to allocate memory for manifest parsing");

        return;
    }

    memcpy(gen_buf, manifest_text, text_len + 1);

    bool pre_malformed = false;

    int expected_entries = count_valid_manifest_entries(gen_buf, &pre_malformed);

    OSI_LOGI(TAG, "[ASYNC-GEN] Pre-scan: %d valid entries found (has_malformed=%d)",

             expected_entries, (int)pre_malformed);

    uint32_t batch_id = 0;

    if (expected_entries > 0)
    {

        batch_id = customer_audio_batch_start(expected_entries);

        if (pre_malformed)
        {

            customer_audio_batch_record_queue_failure(batch_id);
        }
    }

    int total_entries = 0;

    int successful_downloads = 0;

    int failed_downloads = 0;

    char *curr = gen_buf;

    int line_num = 0;

    while (*curr != '\0')
    {

        char *line_start = curr;

        while (*curr != '\0' && *curr != '\r' && *curr != '\n')
        {

            curr++;
        }

        if (*curr == '\r')
        {

            *curr++ = '\0';

            if (*curr == '\n')
            {

                *curr++ = '\0';
            }
        }
        else if (*curr == '\n')
        {

            *curr++ = '\0';
        }

        line_num++;

        trim_whitespace(line_start);

        if (line_start[0] == '\0')
        {

            continue;
        }

        char *pipe = strchr(line_start, '|');

        if (!pipe)
        {

            OSI_LOGE(TAG, "[ASYNC-GEN] Line %d malformed (missing '|' delimiter): '%s'", line_num, line_start);

            failed_downloads++;

            if (batch_id != 0)
            {

                customer_audio_batch_record_queue_failure(batch_id);
            }

            continue;
        }

        *pipe = '\0';

        char *entry_url = line_start;

        char *entry_dst = pipe + 1;

        trim_whitespace(entry_url);

        trim_whitespace(entry_dst);

        if (entry_url[0] == '\0' || entry_dst[0] == '\0')
        {

            OSI_LOGE(TAG, "[ASYNC-GEN] Line %d has empty URL or destination path", line_num);

            failed_downloads++;

            if (batch_id != 0)
            {

                customer_audio_batch_record_queue_failure(batch_id);
            }

            continue;
        }

        total_entries++;

        OSI_LOGI(TAG, "[ASYNC-GEN Entry #%d] %s -> %s", total_entries, entry_url, entry_dst);

        int dl_ret = customer_audio_download_handle_single_mp3(entry_url, entry_dst, default_lang, false);

        if (dl_ret != 0)
        {

            OSI_LOGE(TAG, "[ASYNC-GEN Entry #%d] Failed to queue download (%d)", total_entries, dl_ret);

            failed_downloads++;
        }
        else
        {

            successful_downloads++;
        }
    }

    if (batch_id != 0)
    {

        customer_audio_batch_finish_scheduling(batch_id);
    }
    else
    {
        prv_notify_exception_completion(TMS_EXCEPTION_DOWNLOAD_FAILED);
    }

    free(gen_buf);

    OSI_LOGI(TAG, "[ASYNC-GEN] Scheduling complete: %d queued, %d failed", successful_downloads, failed_downloads);
}

/**

 * @brief Callback when GEN.txt is fetched via http_task

 */

static void prv_tms_gen_http_callback(http_response_t *response, void *user_data)

{

    tms_exception_async_ctx_t *ctx = (tms_exception_async_ctx_t *)user_data;

    if (!response || response->result != HTTP_RESULT_SUCCESS || response->status_code != 200 ||

        !response->response_data || response->response_len == 0)
    {
        prv_notify_exception_completion(TMS_EXCEPTION_DOWNLOAD_FAILED);
        OSI_LOGE(TAG, "[ASYNC-GEN] HTTP GET for GEN.txt failed (status=%d, result=%d)",

                 response ? response->status_code : -1,

                 response ? (int)response->result : -1);

        if (ctx)
            free(ctx);

        return;
    }

    OSI_LOGI(TAG, "[ASYNC-GEN] GEN.txt fetched successfully via HTTP queue (HTTP %d, len=%u)",

             response->status_code, response->response_len);

    prv_parse_and_queue_gen_manifest(response->response_data, ctx ? ctx->default_lang : "EN");

    if (ctx)
    {

        free(ctx);
    }
}

/**

 * @brief Callback when initial Exception manifest (e.g. Exception.txt) is fetched via http_task

 */

static void prv_tms_manifest_http_callback(http_response_t *response, void *user_data)

{

    tms_exception_async_ctx_t *ctx = (tms_exception_async_ctx_t *)user_data;

    if (!response || response->result != HTTP_RESULT_SUCCESS || response->status_code != 200 ||

        !response->response_data || response->response_len == 0)
    {
        prv_notify_exception_completion(TMS_EXCEPTION_DOWNLOAD_FAILED);
        OSI_LOGE(TAG, "[ASYNC-MANIFEST] Initial manifest fetch failed (status=%d, result=%d)",

                 response ? response->status_code : -1,

                 response ? (int)response->result : -1);

        if (ctx)
            free(ctx);

        return;
    }

    OSI_LOGI(TAG, "[ASYNC-MANIFEST] Initial manifest fetched successfully via HTTP queue (HTTP %d, len=%u)",

             response->status_code, response->response_len);

    /* Check if content is already direct GEN.txt format (has '|' delimiter)

     * or if the URL explicitly points to gen.txt */

    bool is_direct_gen = (ctx && (strstr(ctx->manifest_url, "gen.txt") != NULL ||

                                  strstr(ctx->manifest_url, "GEN.txt") != NULL ||

                                  strstr(ctx->manifest_url, "GEN.TXT") != NULL)) ||

                         (strchr(response->response_data, '|') != NULL);

    if (is_direct_gen)
    {

        OSI_LOGI(TAG, "[ASYNC-MANIFEST] Direct audio mappings detected in manifest; parsing entries directly");

        prv_parse_and_queue_gen_manifest(response->response_data, ctx ? ctx->default_lang : "EN");

        if (ctx)
            free(ctx);

        return;
    }

    /* Standard 2-stage TMS manifest: extract GEN.txt URL */

    char gen_url[256] = {0};

    int extract_ret = extract_gen_url_from_manifest(response->response_data, gen_url, sizeof(gen_url));

    if (extract_ret != 0)
    {
        prv_notify_exception_completion(TMS_EXCEPTION_DOWNLOAD_FAILED);
        OSI_LOGE(TAG, "[ASYNC-MANIFEST] Failed to extract GEN.txt URL from response (%d)", extract_ret);

        if (ctx)
            free(ctx);

        return;
    }

    OSI_LOGI(TAG, "[ASYNC-MANIFEST] Extracted GEN.txt URL: %s. Enqueuing HTTP GET...", gen_url);

    http_request_t *gen_req = (http_request_t *)malloc(sizeof(http_request_t));

    if (!gen_req)
    {
        prv_notify_exception_completion(TMS_EXCEPTION_DOWNLOAD_FAILED);
        OSI_LOGE(TAG, "[ASYNC-MANIFEST] Failed to allocate memory for GEN request");

        if (ctx)
            free(ctx);

        return;
    }

    memset(gen_req, 0, sizeof(*gen_req));

    gen_req->method = HTTP_METHOD_GET;

    strncpy(gen_req->url, gen_url, sizeof(gen_req->url) - 1);

    snprintf(gen_req->header, sizeof(gen_req->header), "Accept: text/plain, */*\r\nConnection: close\r\n");

    gen_req->timeout_ms = 15000;

    gen_req->user_data = ctx;

    int q_ret = framework_http_request_async(gen_req, prv_tms_gen_http_callback);

    free(gen_req);

    if (q_ret != 0)
    {
        prv_notify_exception_completion(TMS_EXCEPTION_DOWNLOAD_FAILED);
        OSI_LOGE(TAG, "[ASYNC-MANIFEST] Failed to enqueue GEN.txt HTTP request (%d)", q_ret);

        if (ctx)
            free(ctx);
    }
}

tms_exception_download_status_t tms_exception_download_process(

    int exception_flag,

    const char *manifest_url,

    const char *default_lang

)

{

    OSI_LOGI(TAG, "========================================");

    OSI_LOGI(TAG, "Checking TMS Exception Download Workflow (Async HTTP Queue Mode)");

    OSI_LOGI(TAG, "Exception Flag (15th response index): %d", exception_flag);

    if (exception_flag != 1)
    {

        OSI_LOGI(TAG, "Exception flag is %d (not 1); skipping exception download workflow", exception_flag);

        OSI_LOGI(TAG, "========================================");

        return TMS_EXCEPTION_DOWNLOAD_SKIPPED;
    }

    if (!manifest_url || manifest_url[0] == '\0')
    {

        OSI_LOGE(TAG, "Exception flag is 1, but manifest URL (16th index) is empty; aborting");

        OSI_LOGI(TAG, "========================================");

        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    if (strncmp(manifest_url, "http://", 7) != 0 && strncmp(manifest_url, "https://", 8) != 0)
    {

        OSI_LOGE(TAG, "Exception manifest URL invalid protocol: %s", manifest_url);

        OSI_LOGI(TAG, "========================================");

        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    OSI_LOGI(TAG, "Enqueuing Exception Manifest to HTTP Queue: %s", manifest_url);

    tms_exception_async_ctx_t *ctx = (tms_exception_async_ctx_t *)malloc(sizeof(tms_exception_async_ctx_t));

    if (!ctx)
    {

        OSI_LOGE(TAG, "Failed to allocate memory for async exception context");

        OSI_LOGI(TAG, "========================================");

        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    memset(ctx, 0, sizeof(*ctx));

    strncpy(ctx->manifest_url, manifest_url, sizeof(ctx->manifest_url) - 1);

    if (default_lang && default_lang[0] != '\0')
    {

        strncpy(ctx->default_lang, default_lang, sizeof(ctx->default_lang) - 1);
    }
    else
    {

        strncpy(ctx->default_lang, "EN", sizeof(ctx->default_lang) - 1);
    }

    customer_audio_batch_set_completion_callback(prv_exception_batch_completion_cb, NULL);

    http_request_t *req = (http_request_t *)malloc(sizeof(http_request_t));

    if (!req)
    {

        OSI_LOGE(TAG, "Failed to allocate memory for HTTP request");

        free(ctx);

        OSI_LOGI(TAG, "========================================");

        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    memset(req, 0, sizeof(*req));

    req->method = HTTP_METHOD_GET;

    strncpy(req->url, manifest_url, sizeof(req->url) - 1);

    snprintf(req->header, sizeof(req->header), "Accept: text/plain, */*\r\nConnection: close\r\n");

    req->timeout_ms = 15000;

    req->user_data = ctx;

    int q_ret = framework_http_request_async(req, prv_tms_manifest_http_callback);

    free(req);

    if (q_ret != 0)
    {

        OSI_LOGE(TAG, "Failed to enqueue manifest request to http_task (err=%d)", q_ret);

        free(ctx);

        OSI_LOGI(TAG, "========================================");

        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    OSI_LOGI(TAG, "Successfully queued manifest request to http_task (worker thread with 20KB stack)");

    OSI_LOGI(TAG, "========================================");

    return TMS_EXCEPTION_DOWNLOAD_SUCCESS;
}

tms_exception_download_status_t tms_exception_download_process_sync(
    int exception_flag,
    const char *manifest_url,
    const char *default_lang,
    uint32_t timeout_ms
)
{
    if (exception_flag != 1)
    {
        return TMS_EXCEPTION_DOWNLOAD_SKIPPED;
    }

    if (!manifest_url || manifest_url[0] == '\0')
    {
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    if (s_exception_sync_sem)
    {
        osiSemaphoreDelete(s_exception_sync_sem);
        s_exception_sync_sem = NULL;
    }
    s_exception_sync_sem = osiSemaphoreCreate(1, 0);
    s_exception_sync_result = TMS_EXCEPTION_DOWNLOAD_FAILED;

    customer_audio_batch_set_completion_callback(prv_exception_batch_completion_cb, NULL);

    tms_exception_download_status_t ret = tms_exception_download_process(
        exception_flag,
        manifest_url,
        default_lang
    );

    if (ret != TMS_EXCEPTION_DOWNLOAD_SUCCESS)
    {
        if (s_exception_sync_sem)
        {
            osiSemaphoreDelete(s_exception_sync_sem);
            s_exception_sync_sem = NULL;
        }
        customer_audio_batch_set_completion_callback(NULL, NULL);
        return ret;
    }

    uint32_t wait_ms = (timeout_ms > 0) ? timeout_ms : 60000;
    OSI_LOGI(TAG, "Waiting for full exception download operation (max %u ms)...", (unsigned)wait_ms);
    bool ok = osiSemaphoreTryAcquire(s_exception_sync_sem, wait_ms);
    osiSemaphoreDelete(s_exception_sync_sem);
    s_exception_sync_sem = NULL;
    customer_audio_batch_set_completion_callback(NULL, NULL);

    if (!ok)
    {
        OSI_LOGE(TAG, "Exception download operation timed out (%u ms)", (unsigned)wait_ms);
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    OSI_LOGI(TAG, "Exception download operation completed with result: %d", (int)s_exception_sync_result);
    return s_exception_sync_result;
}

static void prv_tms_delayed_restart_task(void *param)
{
    (void)param;
    OSI_LOGI(TAG, "Device restarting in 2000 ms after download and ACK completion...");
    // osiThreadSleep(2000);
    // osiSysReset();
    osiThreadExit();
}

static void prv_tms_trigger_restart(void)
{
    OSI_LOGI(TAG, "Spawning restart thread after UPDATESBFLAG ACK...");
    osiThread_t *t = osiThreadCreate("tms_rst", prv_tms_delayed_restart_task, NULL, 120, 2048);
    if (!t) {
        OSI_LOGW(TAG, "Failed to create restart thread, executing inline restart");
        // osiThreadSleep(2000);
        // osiSysReset();
    }
}

static void prv_tms_ack_async_callback(http_response_t *response, void *user_data)
{
    bool restart = (bool)(uintptr_t)user_data;
    if (response && response->result == HTTP_RESULT_SUCCESS && response->status_code == 200) {
        OSI_LOGI(TAG, "UPDATESBFLAG ACK confirmed by server (HTTP 200)");
    } else {
        OSI_LOGE(TAG, "UPDATESBFLAG ACK failed (HTTP %d, result=%d)",
                 response ? response->status_code : -1,
                 response ? (int)response->result : -1);
    }

    if (restart) {
        OSI_LOGI(TAG, "Audio files were updated; initiating device restart");
        prv_tms_trigger_restart();
    } else {
        OSI_LOGI(TAG, "No audio updates requested (flags 0,0); ACK confirmed, skipping restart");
    }
}

int tms_send_update_sb_flag_ack(

    const char *base_registration_url,

    const char *serial_no,

    const char *participant_id,

    const char *wel_status,

    const char *exe_status,

    bool restart_on_completion

)

{

    OSI_LOGI(TAG, "========================================");

    OSI_LOGI(TAG, "Preparing TMS UPDATESBFLAG Acknowledgement");

    char ack_url[256] = {0};

    if (base_registration_url && base_registration_url[0] != '\0')
    {

        const char *tms_pos = strstr(base_registration_url, "/TMS/V1/");

        if (tms_pos != NULL)
        {

            size_t base_len = (size_t)(tms_pos - base_registration_url);

            if (base_len < (sizeof(ack_url) - 35))
            {

                strncpy(ack_url, base_registration_url, base_len);

                strcat(ack_url, "/TMS/V1/SoundBoxApp/UPDATESBFLAG");
            }
        }
    }

    if (ack_url[0] == '\0')
    {

        strncpy(ack_url, "https://1.7.140.58/pos/TMS/V1/SoundBoxApp/UPDATESBFLAG", sizeof(ack_url) - 1);
    }

    char sn_buf[32] = {0};

    if (serial_no && serial_no[0] != '\0')
    {

        strncpy(sn_buf, serial_no, sizeof(sn_buf) - 1);
    }
    else
    {

        framework_device_config_get_sn(sn_buf, sizeof(sn_buf));

        if (sn_buf[0] == '\0')
        {

            strncpy(sn_buf, "00001504QR70W031184", sizeof(sn_buf) - 1);
        }
    }

    const char *pid = (participant_id && participant_id[0] != '\0') ? participant_id : "";

    const char *wel_flag = (wel_status && wel_status[0] != '\0') ? wel_status : "0";

    const char *exe_flag = (exe_status && exe_status[0] != '\0') ? exe_status : "0";

    cJSON *json = cJSON_CreateObject();

    if (!json)
    {

        OSI_LOGE(TAG, "Failed to create JSON object for UPDATESBFLAG ACK");

        return -1;
    }

    cJSON_AddStringToObject(json, "serial_no", sn_buf);

    cJSON_AddStringToObject(json, "participantId", pid);

    cJSON_AddStringToObject(json, "welfileflag", wel_flag);

    cJSON_AddStringToObject(json, "exefileflag", exe_flag);

    char *payload_str = cJSON_PrintUnformatted(json);

    cJSON_Delete(json);

    if (!payload_str)
    {

        OSI_LOGE(TAG, "Failed to serialize JSON for UPDATESBFLAG ACK");

        return -2;
    }

    OSI_LOGI(TAG, "Sending UPDATESBFLAG ACK:");

    OSI_LOGI(TAG, "  Endpoint: %s", ack_url);

    OSI_LOGI(TAG, "  Payload:  %s", payload_str);

    /* Use framework_http_post_json so ACK is also non-blocking on the worker queue */

    int ret = framework_http_post_json(ack_url, payload_str, NULL, prv_tms_ack_async_callback, (void *)(uintptr_t)restart_on_completion);

    cJSON_free(payload_str);

    if (ret != 0)
    {

        OSI_LOGE(TAG, "Failed to enqueue UPDATESBFLAG ACK (err=%d)", ret);
        prv_tms_trigger_restart();
    }
    else
    {

        OSI_LOGI(TAG, "UPDATESBFLAG ACK successfully queued to http_task");
    }

    OSI_LOGI(TAG, "========================================");

    return ret;
}
