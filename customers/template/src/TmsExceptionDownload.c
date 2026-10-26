/**
 * @file TmsExceptionDownload.c
 * @brief TMS Exception Audio & File Download Workflow and UPDATESBFLAG ACK Implementation
 */

#include "TmsExceptionDownload.h"
#include "customer_audio_download.h"
#include "customer_mqtt_handler.h"
#include "customer_config.h"
#include "customer_register.h"
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

#define TMS_INITIAL_MANIFEST_BUF_SIZE 4096
#define TMS_GEN_MANIFEST_BUF_SIZE     8192
#define TMS_HTTP_HDR_BUF_SIZE         512
#define TMS_ACK_RESP_BUF_SIZE         1024

static void trim_whitespace(char *str)
{
    if (str == NULL) {
        return;
    }
    char *start = str;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
    if (start != str) {
        memmove(str, start, (size_t)(end - start + 1));
    }
}

/**
 * @brief Execute a synchronous HTTP GET request using httpclient_get
 *
 * @param url Request URL
 * @param resp_buf Output response buffer
 * @param resp_buf_size Size of resp_buf
 * @param out_http_code Output HTTP status code pointer
 * @return 0 on success (HTTP 200), negative error code on failure
 */
static int execute_http_get(const char *url, char *resp_buf, size_t resp_buf_size, int *out_http_code)
{
    if (!url || !resp_buf || resp_buf_size == 0) {
        return -1;
    }

    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    char *hdr_buf = (char *)malloc(TMS_HTTP_HDR_BUF_SIZE);
    if (!hdr_buf) {
        OSI_LOGE(TAG, "Failed to allocate HTTP header buffer");
        return -2;
    }
    memset(hdr_buf, 0, TMS_HTTP_HDR_BUF_SIZE);
    memset(resp_buf, 0, resp_buf_size);

    snprintf(hdr_buf, TMS_HTTP_HDR_BUF_SIZE,
             "Accept: text/plain, */*\r\n"
             "Connection: close\r\n");

    client.header = hdr_buf;
    client.verify_mode = 0; /* Align with registration SSL verify setting */

    client_data.header_buf = hdr_buf;
    client_data.header_buf_len = TMS_HTTP_HDR_BUF_SIZE;
    client_data.response_buf = resp_buf;
    client_data.response_buf_len = resp_buf_size - 1;

    HTTPCLIENT_RESULT http_res = httpclient_get(&client, (char *)url, &client_data);
    int status_code = httpclient_get_response_code(&client);

    if (out_http_code) {
        *out_http_code = status_code;
    }

    free(hdr_buf);
    httpclient_close(&client);

    if (http_res != HTTPCLIENT_OK || status_code != 200) {
        OSI_LOGE(TAG, "HTTP GET failed: url=%s, res=%d, http_code=%d",
                 url, (int)http_res, status_code);
        return -3;
    }

    resp_buf[resp_buf_size - 1] = '\0';
    return 0;
}

/**
 * @brief Extract the GEN.txt URL from the initial manifest body
 *
 * The initial manifest contains the URL pointing to GEN.txt.
 * Handles both plain URLs and any surrounding whitespace/delimiters.
 */
static int extract_gen_url_from_manifest(const char *manifest_body, char *out_url, size_t max_len)
{
    if (!manifest_body || !out_url || max_len == 0) {
        return -1;
    }

    const char *url_start = strstr(manifest_body, "http://");
    if (!url_start) {
        url_start = strstr(manifest_body, "https://");
    }

    if (!url_start) {
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
    if (out_url[0] == '\0') {
        OSI_LOGE(TAG, "Extracted empty GEN.txt URL from manifest");
        return -3;
    }

    return 0;
}

const char *tms_map_ack_status(bool requested, bool success)
{
    if (!requested) {
        return TMS_ACK_FLAG_SKIPPED;
    }
    return success ? TMS_ACK_FLAG_SUCCESS : TMS_ACK_FLAG_FAILED;
}

static int count_valid_manifest_entries(const char *manifest_text, bool *out_has_malformed)
{
    if (!manifest_text) {
        return 0;
    }

    int count = 0;
    bool malformed = false;
    const char *p = manifest_text;

    while (*p != '\0') {
        while (*p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char *line_start = p;
        while (*p != '\0' && *p != '\r' && *p != '\n') {
            p++;
        }

        const char *pipe = NULL;
        bool non_space = false;
        for (const char *s = line_start; s < p; s++) {
            if (*s == '|') {
                pipe = s;
            } else if (!isspace((unsigned char)*s)) {
                non_space = true;
            }
        }

        if (!non_space) {
            continue;
        }

        if (!pipe) {
            malformed = true;
            continue;
        }

        bool url_has_chars = false;
        for (const char *s = line_start; s < pipe; s++) {
            if (!isspace((unsigned char)*s)) {
                url_has_chars = true;
                break;
            }
        }

        bool dst_has_chars = false;
        for (const char *s = pipe + 1; s < p; s++) {
            if (!isspace((unsigned char)*s)) {
                dst_has_chars = true;
                break;
            }
        }

        if (url_has_chars && dst_has_chars) {
            count++;
        } else {
            malformed = true;
        }
    }

    if (out_has_malformed) {
        *out_has_malformed = malformed;
    }
    return count;
}

tms_exception_download_status_t tms_exception_download_process(
    int exception_flag,
    const char *manifest_url,
    const char *default_lang
)
{
    /* 1. Validate Exception Flag */
    OSI_LOGI(TAG, "========================================");
    OSI_LOGI(TAG, "Checking TMS Exception Download Workflow");
    OSI_LOGI(TAG, "Exception Flag (15th response index): %d", exception_flag);

    if (exception_flag != 1) {
        OSI_LOGI(TAG, "Exception flag is %d (not 1); skipping exception download workflow", exception_flag);
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_SKIPPED;
    }

    /* 2. Validate Initial Manifest URL */
    if (!manifest_url || manifest_url[0] == '\0') {
        OSI_LOGE(TAG, "Exception flag is 1, but manifest URL (16th index) is empty; aborting");
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    if (strncmp(manifest_url, "http://", 7) != 0 && strncmp(manifest_url, "https://", 8) != 0) {
        OSI_LOGE(TAG, "Exception manifest URL invalid protocol: %s", manifest_url);
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    OSI_LOGI(TAG, "Initial Exception Manifest URL: %s", manifest_url);

    /* 3. Fetch Initial Exception Manifest */
    char *initial_buf = (char *)malloc(TMS_INITIAL_MANIFEST_BUF_SIZE);
    if (!initial_buf) {
        OSI_LOGE(TAG, "Memory allocation failed for initial manifest buffer");
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    int http_code = 0;
    int get_ret = execute_http_get(manifest_url, initial_buf, TMS_INITIAL_MANIFEST_BUF_SIZE, &http_code);
    if (get_ret != 0) {
        OSI_LOGE(TAG, "Initial exception manifest GET failed: ret=%d, http_code=%d", get_ret, http_code);
        free(initial_buf);
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    OSI_LOGI(TAG, "Initial manifest fetched successfully (HTTP %d, len=%u)",
             http_code, (unsigned int)strlen(initial_buf));

    /* 4. Extract GEN.txt URL from initial manifest response */
    char gen_url[256] = {0};
    int extract_ret = extract_gen_url_from_manifest(initial_buf, gen_url, sizeof(gen_url));
    free(initial_buf);
    initial_buf = NULL;

    if (extract_ret != 0) {
        OSI_LOGE(TAG, "Failed to extract GEN.txt URL from initial manifest: %d", extract_ret);
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    OSI_LOGI(TAG, "Extracted GEN.txt URL: %s", gen_url);

    /* 5. Download GEN.txt Manifest */
    char *gen_buf = (char *)malloc(TMS_GEN_MANIFEST_BUF_SIZE);
    if (!gen_buf) {
        OSI_LOGE(TAG, "Memory allocation failed for GEN.txt manifest buffer");
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    get_ret = execute_http_get(gen_url, gen_buf, TMS_GEN_MANIFEST_BUF_SIZE, &http_code);
    if (get_ret != 0) {
        OSI_LOGE(TAG, "GEN.txt GET request failed: ret=%d, http_code=%d", get_ret, http_code);
        free(gen_buf);
        OSI_LOGI(TAG, "========================================");
        return TMS_EXCEPTION_DOWNLOAD_FAILED;
    }

    OSI_LOGI(TAG, "GEN.txt fetched successfully (HTTP %d, len=%u)",
             http_code, (unsigned int)strlen(gen_buf));

    /* 6. Parse GEN.txt line by line (<download_url>|<destination_path>) */
    bool pre_malformed = false;
    int expected_entries = count_valid_manifest_entries(gen_buf, &pre_malformed);
    OSI_LOGI(TAG, "GEN.txt pre-scan: %d valid entries found (has_malformed=%d)",
             expected_entries, (int)pre_malformed);

    uint32_t batch_id = 0;
    if (expected_entries > 0) {
        batch_id = customer_audio_batch_start(expected_entries);
        if (pre_malformed) {
            customer_audio_batch_record_queue_failure(batch_id);
        }
    }

    int total_entries = 0;
    int successful_downloads = 0;
    int failed_downloads = 0;
    bool has_malformed_entries = pre_malformed;

    char *curr = gen_buf;
    int line_num = 0;

    while (*curr != '\0') {
        char *line_start = curr;

        /* Find line ending (\r, \n, or \0) */
        while (*curr != '\0' && *curr != '\r' && *curr != '\n') {
            curr++;
        }

        /* Null-terminate the line */
        if (*curr == '\r') {
            *curr++ = '\0';
            if (*curr == '\n') {
                *curr++ = '\0';
            }
        } else if (*curr == '\n') {
            *curr++ = '\0';
        }

        line_num++;
        trim_whitespace(line_start);

        /* Skip empty lines */
        if (line_start[0] == '\0') {
            continue;
        }

        /* Locate '|' delimiter */
        char *pipe = strchr(line_start, '|');
        if (!pipe) {
            OSI_LOGE(TAG, "GEN.txt Line %d malformed (missing '|' delimiter): '%s'", line_num, line_start);
            has_malformed_entries = true;
            failed_downloads++;
            if (batch_id != 0) {
                customer_audio_batch_record_queue_failure(batch_id);
            }
            continue;
        }

        *pipe = '\0';
        char *entry_url = line_start;
        char *entry_dst = pipe + 1;

        trim_whitespace(entry_url);
        trim_whitespace(entry_dst);

        if (entry_url[0] == '\0' || entry_dst[0] == '\0') {
            OSI_LOGE(TAG, "GEN.txt Line %d has empty URL or destination path (url='%s', dst='%s')",
                     line_num, entry_url, entry_dst);
            has_malformed_entries = true;
            failed_downloads++;
            if (batch_id != 0) {
                customer_audio_batch_record_queue_failure(batch_id);
            }
            continue;
        }

        total_entries++;
        OSI_LOGI(TAG, "[GEN Entry #%d] URL=%s -> DST=%s", total_entries, entry_url, entry_dst);

        /* Call the single MP3 download handler with restart_after_download = false */
        int dl_ret = customer_audio_download_handle_single_mp3(entry_url, entry_dst, default_lang, false);
        if (dl_ret != 0) {
            OSI_LOGE(TAG, "[GEN Entry #%d] Handler failed to queue download (ret=%d): %s -> %s",
                     total_entries, dl_ret, entry_url, entry_dst);
            failed_downloads++;
        } else {
            OSI_LOGI(TAG, "[GEN Entry #%d] Successfully queued download: %s -> %s",
                     total_entries, entry_url, entry_dst);
            successful_downloads++;
        }
    }

    if (batch_id != 0) {
        customer_audio_batch_finish_scheduling(batch_id);
    }

    free(gen_buf);
    gen_buf = NULL;

    /* 7. Summary and Overall Result */
    OSI_LOGI(TAG, "----------------------------------------");
    OSI_LOGI(TAG, "GEN.txt Processing Summary:");
    OSI_LOGI(TAG, "  Total Valid Entries Processed: %d", total_entries);
    OSI_LOGI(TAG, "  Successfully Queued Downloads: %d", successful_downloads);
    OSI_LOGI(TAG, "  Failed Downloads / Entries:    %d", failed_downloads);

    tms_exception_download_status_t final_status;
    if (total_entries > 0 && failed_downloads == 0 && !has_malformed_entries) {
        final_status = TMS_EXCEPTION_DOWNLOAD_SUCCESS;
        OSI_LOGI(TAG, "Overall Exception Download Status: SUCCESS");
    } else {
        final_status = TMS_EXCEPTION_DOWNLOAD_FAILED;
        OSI_LOGE(TAG, "Overall Exception Download Status: FAILED (total=%d, failed=%d, malformed=%d)",
                 total_entries, failed_downloads, (int)has_malformed_entries);
    }
    OSI_LOGI(TAG, "========================================");

    return final_status;
}

int tms_send_update_sb_flag_ack(
    const char *base_registration_url,
    const char *serial_no,
    const char *participant_id,
    const char *wel_status,
    const char *exe_status
)
{
    OSI_LOGI(TAG, "========================================");
    OSI_LOGI(TAG, "Preparing TMS UPDATESBFLAG Acknowledgement");

    /* 1. Construct endpoint: <Base URL>/TMS/V1/SoundBoxApp/UPDATESBFLAG */
    char ack_url[256] = {0};
    if (base_registration_url && base_registration_url[0] != '\0') {
        const char *tms_pos = strstr(base_registration_url, "/TMS/V1/");
        if (tms_pos != NULL) {
            size_t base_len = (size_t)(tms_pos - base_registration_url);
            if (base_len < (sizeof(ack_url) - 35)) {
                strncpy(ack_url, base_registration_url, base_len);
                strcat(ack_url, "/TMS/V1/SoundBoxApp/UPDATESBFLAG");
            }
        }
    }
    if (ack_url[0] == '\0') {
        strncpy(ack_url, "https://1.7.140.58/pos/TMS/V1/SoundBoxApp/UPDATESBFLAG", sizeof(ack_url) - 1);
    }

    /* 2. Resolve Serial Number */
    char sn_buf[32] = {0};
    if (serial_no && serial_no[0] != '\0') {
        strncpy(sn_buf, serial_no, sizeof(sn_buf) - 1);
    } else {
        framework_device_config_get_sn(sn_buf, sizeof(sn_buf));
        if (sn_buf[0] == '\0') {
            strncpy(sn_buf, "00001504QR70W031184", sizeof(sn_buf) - 1);
        }
    }

    const char *pid = (participant_id && participant_id[0] != '\0') ? participant_id : "";
    const char *wel_flag = (wel_status && wel_status[0] != '\0') ? wel_status : "0";
    const char *exe_flag = (exe_status && exe_status[0] != '\0') ? exe_status : "0";

    /* 3. Build JSON payload */
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        OSI_LOGE(TAG, "Failed to create JSON object for UPDATESBFLAG ACK");
        return -1;
    }

    cJSON_AddStringToObject(json, "serial_no", sn_buf);
    cJSON_AddStringToObject(json, "participantId", pid);
    cJSON_AddStringToObject(json, "welfileflag", wel_flag);
    cJSON_AddStringToObject(json, "exefileflag", exe_flag);

    char *payload_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!payload_str) {
        OSI_LOGE(TAG, "Failed to serialize JSON for UPDATESBFLAG ACK");
        return -2;
    }

    OSI_LOGI(TAG, "Sending UPDATESBFLAG ACK:");
    OSI_LOGI(TAG, "  Endpoint: %s", ack_url);
    OSI_LOGI(TAG, "  Payload:  %s", payload_str);

    /* 4. Execute HTTP POST */
    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    char *hdr_buf = (char *)malloc(TMS_HTTP_HDR_BUF_SIZE);
    char *resp_buf = (char *)malloc(TMS_ACK_RESP_BUF_SIZE);

    if (!hdr_buf || !resp_buf) {
        OSI_LOGE(TAG, "Failed to allocate buffers for ACK request");
        cJSON_free(payload_str);
        if (hdr_buf) free(hdr_buf);
        if (resp_buf) free(resp_buf);
        return -3;
    }

    memset(hdr_buf, 0, TMS_HTTP_HDR_BUF_SIZE);
    memset(resp_buf, 0, TMS_ACK_RESP_BUF_SIZE);

    snprintf(hdr_buf, TMS_HTTP_HDR_BUF_SIZE,
             "Content-Type: application/json\r\n"
             "Accept: application/json, text/plain, */*\r\n");

    client.header = hdr_buf;
    client.verify_mode = 0;

    client_data.header_buf = hdr_buf;
    client_data.header_buf_len = TMS_HTTP_HDR_BUF_SIZE;
    client_data.post_buf = payload_str;
    client_data.post_buf_len = strlen(payload_str);
    client_data.response_buf = resp_buf;
    client_data.response_buf_len = TMS_ACK_RESP_BUF_SIZE - 1;

    HTTPCLIENT_RESULT res = httpclient_post(&client, ack_url, &client_data);
    int status_code = httpclient_get_response_code(&client);

    cJSON_free(payload_str);
    free(hdr_buf);

    int final_ret = -4;
    if (res == HTTPCLIENT_OK && status_code == 200) {
        resp_buf[TMS_ACK_RESP_BUF_SIZE - 1] = '\0';
        OSI_LOGI(TAG, "UPDATESBFLAG ACK Success (HTTP 200). Response: %s", resp_buf);
        final_ret = 0;
    } else {
        OSI_LOGE(TAG, "UPDATESBFLAG ACK Failed: res=%d, http_code=%d", (int)res, status_code);
    }

    free(resp_buf);
    httpclient_close(&client);
    OSI_LOGI(TAG, "========================================");

    return final_ret;
}
