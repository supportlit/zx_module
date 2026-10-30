#include "customer_register.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

/* ZX Module SDK headers */
#include "httpclient.h"
#include "framework_log.h"
#include "framework_network.h"
#include "framework_audio.h"
#include "framework_http.h"
#include "framework_msg.h"
#include "audio_resource_map.h"
#include "customer_mqtt_handler.h"
#include "customer_status_report.h"
#include "cJSON.h"
#include "customer_app_update.h"
#include "customer_config.h"
#include "customer_audio_download.h"
#include "fs_api.h"
#include "TmsExceptionDownload.h"
#include "osi_api.h"
#include "framework_device_config.h"
#include "soundbox_language.h"
#include "customer_lang_download.h"
#include "customer_change_language_ack.h"
#define MAX_REDIRECT_HOPS 3
#define TAG "TMS_REG"
#define TMS_REGISTRATION_URL \
    "https://1.7.140.58/pos/TMS/V1/SoundBoxManagement/SBLogin"
// #define TMS_REGISTRATION_URL
//     "https://script.google.com/macros/s/AKfycbyb43uMq36LyLPdg3I1BRsf5PqosOi0Igu3YwXVz2rA0IWoJizPMWE_8Kswt6tUtXFS/exec"
#define TMS_MAX_FIELDS 32
#define TMS_RESPONSE_BUFFER_SIZE 4096
#define TMS_HEADER_BUFFER_SIZE 512
#define TMS_RETRY_INITIAL_DELAY_MS 5000U
#define TMS_RETRY_MAX_DELAY_MS 60000U

/* Replace this placeholder with the actual TMS registration-accepted code. */
#define TMS_REGISTRATION_SUCCESS_CODE "0000"

/* 0 means retry indefinitely; otherwise stop after this many attempts. */
#define TMS_MAX_RETRIES 0U

/* Standard JSON POST request body. Replace hardcoded device values as needed. */
static const char *s_hardcoded_json_req =
    "{"
    "\"serial_no\":\"BP02869683086200139\","
    "\"app_version\":\"2.0.0.6\","
    "\"audio_version\":\"MP3_V1.0_20221116\","
    "\"TMSIPPort\":\"https://1.7.140.58/pos\","
    "\"IMEI\":\"869683086200139\","
    "\"SIMNo\":\"89914526034074312745\","
    "\"firmware_version\":\"v1.2.0\","
    "\"active_language\":\"0\""
    "}";

static char s_tms_partner_id[64] = {0};
static char s_tms_app_code[64] = {0};

const char *customer_register_get_partner_id(void)
{
    if (s_tms_partner_id[0] != '\0')
        return s_tms_partner_id;
    if (mqtt_reg.partner_id[0] != '\0')
        return mqtt_reg.partner_id;
    return "b15eeee6-b841-44af-9448-2c58d2cadb11";
}

const char *customer_register_get_app_code(void)
{
    if (s_tms_app_code[0] != '\0')
        return s_tms_app_code;
    return "HPRCEKSPFI";
}

const char *customer_register_get_base_url(void)
{
    return "https://1.7.140.58/pos";
}

#define TEST_FILE_PATH "D:/test.txt"
#define TEST_FILE_TMP_PATH "D:/test.tmp"

/* Runtime guard to prevent duplicate/concurrent test downloads */
static bool s_test_download_in_progress = false;

/* Case A: Exact test registration response when TEST_FLAG=0 (code 9999, welcome audio download) */
static const char *s_test_registration_resp __attribute__((unused)) =
    "9999|1.7.140.58|9000|admin|Pine@987|0|ABCD||"
    "b15eeee6-b841-44af-9448-2c58d2cadb11|HPRCEKSPFI|"
    "0|0|0||0||1|1|1|0|0|";

/* Case B: Exact normal registration response when TEST_FLAG=1 (code 0000, skip test) */
static const char *s_normal_registration_resp =
    "0000|1.7.140.58|9000|admin|Pine@987|0|ABCD||"
    "b15eeee6-b841-44af-9448-2c58d2cadb11|HPRCEKSPFI|"
    "0|0|0||0||1|0|0|0|0|";

/**
 * @brief Read TEST_FLAG value from persistent file "D:/test.txt".
 * @return 0 if TEST_FLAG=0, 1 if TEST_FLAG=1, -1 if file missing, -2 if unreadable/malformed.
 */
static __attribute__((unused)) int customer_test_read_flag(void)
{
    int fd = fs_open(TEST_FILE_PATH, "rb");
    if (fd <= 0)
    {
        return -1;
    }

    char buf[64] = {0};
    size_t read_bytes = fs_read(buf, 1, sizeof(buf) - 1, fd);
    fs_close(fd);

    if (read_bytes == 0)
    {
        OSI_LOGE(TAG, "[TEST_FLAG] %s is empty", TEST_FILE_PATH);
        return -2;
    }
    buf[read_bytes] = '\0';

    char *ptr = strstr(buf, "TEST_FLAG=");
    if (!ptr)
    {
        OSI_LOGE(TAG, "[TEST_FLAG] Malformed content in %s (missing 'TEST_FLAG='): '%s'",
                 TEST_FILE_PATH, buf);
        return -2;
    }

    ptr += strlen("TEST_FLAG=");
    while (*ptr == ' ' || *ptr == '\t')
        ptr++;

    if (*ptr == '0')
    {
        OSI_LOGI(TAG, "[TEST_FLAG] Read TEST_FLAG=0 from %s", TEST_FILE_PATH);
        return 0;
    }
    else if (*ptr == '1')
    {
        OSI_LOGI(TAG, "[TEST_FLAG] Read TEST_FLAG=1 from %s", TEST_FILE_PATH);
        return 1;
    }
    else
    {
        OSI_LOGE(TAG, "[TEST_FLAG] Malformed flag value in %s: '%c'", TEST_FILE_PATH, *ptr);
        return -2;
    }
}

/**
 * @brief Safely write TEST_FLAG to persistent storage using temp file + atomic rename.
 * @param val 0 or 1
 * @return 0 on success, negative error code on failure.
 */
static int customer_test_write_flag(int val)
{
    char write_buf[32] = {0};
    snprintf(write_buf, sizeof(write_buf), "TEST_FLAG=%d\r\n", val);
    size_t write_len = strlen(write_buf);

    fs_remove(TEST_FILE_TMP_PATH);
    int fd = fs_open(TEST_FILE_TMP_PATH, "wb+");
    if (fd <= 0)
    {
        OSI_LOGW(TAG, "[TEST_FLAG] Cannot open temp file %s (fd=%d), attempting direct write to %s",
                 TEST_FILE_TMP_PATH, fd, TEST_FILE_PATH);
        fd = fs_open(TEST_FILE_PATH, "wb+");
        if (fd <= 0)
        {
            OSI_LOGE(TAG, "[TEST_FLAG] Failed to open %s for write (fd=%d)", TEST_FILE_PATH, fd);
            return -1;
        }
        size_t written = fs_write(write_buf, 1, write_len, fd);
        fs_sync(fd);
        fs_close(fd);
        if (written != write_len)
        {
            OSI_LOGE(TAG, "[TEST_FLAG] Direct write incomplete (written=%u, expected=%u)",
                     (unsigned int)written, (unsigned int)write_len);
            return -2;
        }
        OSI_LOGI(TAG, "[TEST_FLAG] Successfully persisted TEST_FLAG=%d directly to %s",
                 val, TEST_FILE_PATH);
        return 0;
    }

    size_t written = fs_write(write_buf, 1, write_len, fd);
    fs_sync(fd);
    fs_close(fd);

    if (written != write_len)
    {
        OSI_LOGE(TAG, "[TEST_FLAG] Temp write incomplete (written=%u, expected=%u)",
                 (unsigned int)written, (unsigned int)write_len);
        fs_remove(TEST_FILE_TMP_PATH);
        return -2;
    }

    fs_remove(TEST_FILE_PATH);
    int ren_ret = fs_rename(TEST_FILE_TMP_PATH, TEST_FILE_PATH);
    if (ren_ret != 0)
    {
        OSI_LOGW(TAG, "[TEST_FLAG] fs_rename failed (%d), writing directly to %s", ren_ret, TEST_FILE_PATH);
        fd = fs_open(TEST_FILE_PATH, "wb+");
        if (fd > 0)
        {
            fs_write(write_buf, 1, write_len, fd);
            fs_sync(fd);
            fs_close(fd);
            fs_remove(TEST_FILE_TMP_PATH);
            OSI_LOGI(TAG, "[TEST_FLAG] Successfully persisted TEST_FLAG=%d to %s via fallback",
                     val, TEST_FILE_PATH);
            return 0;
        }
        OSI_LOGE(TAG, "[TEST_FLAG] Failed to replace %s", TEST_FILE_PATH);
        return -3;
    }

    OSI_LOGI(TAG, "[TEST_FLAG] Successfully persisted TEST_FLAG=%d to %s",
             val, TEST_FILE_PATH);
    return 0;
}

/**
 * @brief Boot initialization of the test flag file.
 * Creates "D:/test.txt" with TEST_FLAG=0 if it does not exist.
 * @return 0 if TEST_FLAG is 0, 1 if TEST_FLAG is 1, negative on error.
 */
int customer_test_init(void)
{
    OSI_LOGI(TAG, "[TEST_FLAG] Boot initialization: Checking %s (Macro TMS_REGISTRATION_TEST_ENABLE=%d)",
             TEST_FILE_PATH, TMS_REGISTRATION_TEST_ENABLE);

#if defined(TMS_REGISTRATION_TEST_ENABLE) && (TMS_REGISTRATION_TEST_ENABLE == 0)
    /* Normal Mode configured via macro */
    OSI_LOGI(TAG, "[TEST_FLAG] TMS_REGISTRATION_TEST_ENABLE is 0: Forcing Normal Mode (TEST_FLAG=1)");
    customer_test_write_flag(1);
    return 1;
#else
    int flag = customer_test_read_flag();
    if (flag == -1)
    {
        OSI_LOGI(TAG, "[TEST_FLAG] %s not found on boot. Creating with TEST_FLAG=0", TEST_FILE_PATH);
        int write_res = customer_test_write_flag(0);
        if (write_res != 0)
        {
            OSI_LOGE(TAG, "[TEST_FLAG] Failed to create initial %s (ret=%d)", TEST_FILE_PATH, write_res);
            return -1;
        }
        OSI_LOGI(TAG, "[TEST_FLAG] %s initialized successfully with TEST_FLAG=0", TEST_FILE_PATH);
        return 0;
    }
    else if (flag == 0)
    {
        OSI_LOGI(TAG, "[TEST_FLAG] %s exists with TEST_FLAG=0 (test pending)", TEST_FILE_PATH);
        return 0;
    }
    else if (flag == 1)
    {
        OSI_LOGI(TAG, "[TEST_FLAG] %s exists with TEST_FLAG=1 (test completed)", TEST_FILE_PATH);
        return 1;
    }
    else
    {
        OSI_LOGE(TAG, "[TEST_FLAG] %s unreadable or malformed (err=%d); safely writing TEST_FLAG=0",
                 TEST_FILE_PATH, flag);
        customer_test_write_flag(0);
        return 0;
    }
#endif
}

/**
 * @brief Get the current test flag value. Never assumes test succeeded if unreadable.
 * @return 0 (test path) or 1 (normal path).
 */
int customer_test_get_flag(void)
{
#if defined(TMS_REGISTRATION_TEST_ENABLE) && (TMS_REGISTRATION_TEST_ENABLE == 0)
    return 1; /* Normal Mode */
#else
    int flag = customer_test_read_flag();
    if (flag < 0)
    {
        OSI_LOGE(TAG, "[TEST_FLAG] Read failed (%d); defaulting to TEST_FLAG=0 (never assume test succeeded)", flag);
        return 0;
    }
    return flag;
#endif
}

/**
 * @brief Explicitly reset test flag to 0 for debugging / test restart.
 */
void customer_register_clear_test_flag(void)
{
    OSI_LOGI(TAG, "[TEST_FLAG] Explicit reset requested; setting TEST_FLAG=0 in %s", TEST_FILE_PATH);
    customer_test_write_flag(0);
}

static void trim_whitespace(char *str)
{
    char *start;
    char *end;

    if (str == NULL || *str == '\0')
        return;

    start = str;
    while (*start != '\0' && isspace((unsigned char)*start))
        start++;

    if (start != str)
        memmove(str, start, strlen(start) + 1U);

    if (*str == '\0')
        return;

    end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }
}

/* Preserves empty fields, including trailing empty fields, in pipe responses. */
static int parse_pipe_tokens(char *input, char *tokens[], int max_tokens)
{
    int count = 0;
    char *curr;

    if (input == NULL || tokens == NULL || max_tokens <= 0)
        return 0;

    curr = input;
    while (count < max_tokens)
    {
        char *pipe;
        tokens[count++] = curr;
        pipe = strchr(curr, '|');
        if (pipe == NULL)
            break;
        *pipe = '\0';
        curr = pipe + 1;
    }

    for (int i = 0; i < count; i++)
        trim_whitespace(tokens[i]);

    return count;
}

/* Maps the server's pipe-separated response into the existing response struct. */
static int parse_tms_registration_response(const char *raw_data,
                                           tms_registration_resp_t *resp)
{
    char buf[1024];
    char *tok[TMS_MAX_FIELDS] = {0};
    size_t raw_len;
    int total;
    if (raw_data != NULL)
    {
        OSI_LOGI(TAG, "TMS raw response received; length=%u", (unsigned int)strlen(raw_data));
        log_string_in_chunks_with_label(TAG, "TMS_RAW_RESP", raw_data);
    }
    if (raw_data == NULL || resp == NULL)
        return -1;

    memset(resp, 0, sizeof(*resp));
    raw_len = strlen(raw_data);
    if (raw_len == 0U)
    {
        OSI_LOGE(TAG, "Empty TMS response");
        return -2;
    }
    if (raw_len >= sizeof(buf))
    {
        OSI_LOGE(TAG, "TMS response too long for parser: %u",
                 (unsigned int)raw_len);
        return -2;
    }

    memcpy(buf, raw_data, raw_len + 1U);
    total = parse_pipe_tokens(buf, tok, TMS_MAX_FIELDS);
    OSI_LOGI(TAG, "Parsed %d tokens from server response", total);

    /* The existing protocol mapping requires at least the first 10 fields. */
    if (total < 10)
    {
        OSI_LOGE(TAG, "Malformed response: insufficient fields (%d)", total);
        return -2;
    }

    if (total > 0)
        strncpy(resp->response_code, tok[0], sizeof(resp->response_code) - 1U);
    if (total > 1)
        strncpy(resp->mqtt_ip, tok[1], sizeof(resp->mqtt_ip) - 1U);
    if (total > 2)
        resp->mqtt_port = atoi(tok[2]);
    if (total > 3)
        strncpy(resp->mqtt_user, tok[3], sizeof(resp->mqtt_user) - 1U);
    if (total > 4)
        strncpy(resp->mqtt_pass, tok[4], sizeof(resp->mqtt_pass) - 1U);
    if (total > 5)
        resp->mqtt_ssl_enable = atoi(tok[5]);
    if (total > 6)
        strncpy(resp->mqtt_publish_key, tok[6], sizeof(resp->mqtt_publish_key) - 1U);
    if (total > 7)
        strncpy(resp->app_download_url, tok[7], sizeof(resp->app_download_url) - 1U);
    if (total > 8)
        strncpy(resp->participant_id, tok[8], sizeof(resp->participant_id) - 1U);
    if (total > 9)
        strncpy(resp->application_code, tok[9], sizeof(resp->application_code) - 1U);
    if (total > 10)
        resp->txn_ack_flag = atoi(tok[10]);
    if (total > 11)
        resp->pending_txn_flag = atoi(tok[11]);
    if (total > 12)
        resp->welcome_file_flag = atoi(tok[12]);
    if (total > 13)
        strncpy(resp->welcome_file_url, tok[13], sizeof(resp->welcome_file_url) - 1U);
    if (total > 14)
        resp->exception_file_flag = atoi(tok[14]);
    if (total > 15)
        strncpy(resp->exception_file_url, tok[15], sizeof(resp->exception_file_url) - 1U);
    if (total > 16)
        resp->log_flag = atoi(tok[16]);
    if (total > 17)
        resp->last_txn_flag = atoi(tok[17]);
    if (total > 18)
        resp->lang_download_flag = atoi(tok[18]);
    if (total > 19)
        resp->lang_numeric_code = atoi(tok[19]);
    if (total > 20)
        strncpy(resp->change_lang_req_id, tok[20], sizeof(resp->change_lang_req_id) - 1U);
    if (total > 21)
        strncpy(resp->future_use, tok[21], sizeof(resp->future_use) - 1U);

    return 0;
}

/* Performs one HTTP registration attempt. 0 means HTTP succeeded and parsed;
 * it does NOT by itself mean the TMS server approved the device. */
int tms_register_device_sync(tms_registration_resp_t *out_resp)
{
    char *resp_buf = NULL;
    char *hdr_buf = NULL;
    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    HTTPCLIENT_RESULT http_res;
    int ret_status = -1;

    if (out_resp == NULL)
        return -1;

    memset(out_resp, 0, sizeof(*out_resp));

    resp_buf = (char *)malloc(TMS_RESPONSE_BUFFER_SIZE);
    hdr_buf = (char *)malloc(TMS_HEADER_BUFFER_SIZE);
    if (resp_buf == NULL || hdr_buf == NULL)
    {
        OSI_LOGE(TAG, "Failed to allocate memory for HTTP transaction");
        ret_status = -2;
        goto cleanup;
    }

    memset(resp_buf, 0, TMS_RESPONSE_BUFFER_SIZE);
    memset(hdr_buf, 0, TMS_HEADER_BUFFER_SIZE);

    snprintf(hdr_buf, TMS_HEADER_BUFFER_SIZE,
             "Content-Type: application/json\r\n"
             "Accept: text/plain, */*\r\n");

    client.header = hdr_buf;

    /* VERIFY_NONE is retained from the original code for compatibility.
     * Production firmware should verify the server certificate properly. */
    client.verify_mode = 0;
    char json_request[512];
    char sn[32] = {0};
    if (framework_device_config_get_sn(sn, sizeof(sn)) == 0)
    {
        OSI_LOGI("APP", "Device SN: %s", sn);
    }
    else
    {
        OSI_LOGE("APP", "Failed to get Device SN");
    }
    char curr_lan[5];

    snprintf(curr_lan, sizeof(curr_lan), "%d", SOUNDBOX_DEFAULT_LANGUAGE);
    snprintf(json_request, sizeof(json_request),
             "{"
             "\"serial_no\":\"%s\","
             "\"app_version\":\"1.0.0.2\","
             "\"audio_version\":\"MP3_V1.0_20221116\","
             "\"TMSIPPort\":\"https://1.7.140.58/pos\","
             "\"IMEI\":\"%s\","
             "\"SIMNo\":\"%s\","
             "\"firmware_version\":\"v1.2.0\","
             "\"active_language\":\"%s\""
             "}",
             sn,
             framework_network_get_imei(),
             framework_network_get_iccid(),
             curr_lan);
    client_data.post_buf = (char *)json_request;
    client_data.post_buf_len = strlen(json_request);
    client_data.response_buf = resp_buf;
    client_data.response_buf_len = TMS_RESPONSE_BUFFER_SIZE - 1U;

    OSI_LOGI(TAG, "Starting TMS registration POST");
    OSI_LOGI(TAG, "Registration endpoint: %s", TMS_REGISTRATION_URL);
    log_string_in_chunks_with_label(TAG, "TMS_REQ_BODY", s_hardcoded_json_req);
    /* Avoid logging IMEI/SIM and the complete request body in production. */

    http_res = httpclient_post(&client, TMS_REGISTRATION_URL, &client_data);
    resp_buf[TMS_RESPONSE_BUFFER_SIZE - 1U] = '\0';

    if (http_res != HTTPCLIENT_OK || client.response_code != 200)
    {
        OSI_LOGE(TAG, "HTTP registration failed: result=%d, HTTP status=%d",
                 (int)http_res, client.response_code);
        ret_status = -4;
        goto cleanup;
    }

    OSI_LOGI(TAG, "HTTP 200 received; response length=%u",
             (unsigned int)strlen(resp_buf));

    const char *selected_resp = NULL;
#if defined(TMS_REGISTRATION_TEST_ENABLE) && (TMS_REGISTRATION_TEST_ENABLE == 0)
    OSI_LOGI(TAG, "[REG-SELECT] Macro TMS_REGISTRATION_TEST_ENABLE=0 -> Direct Case B: Normal registration response (code 0000)");
    selected_resp = s_normal_registration_resp;
#else
    int test_flag = customer_test_get_flag();
    if (test_flag == 1)
    {
        OSI_LOGI(TAG, "[REG-SELECT] TEST_FLAG=1 -> Selected Case B: Normal registration response (code 0000)");
        selected_resp = s_normal_registration_resp;
    }
    else
    {
        OSI_LOGI(TAG, "[REG-SELECT] TEST_FLAG=%d -> Selected Case A: Test registration response (code 9999, welcome audio download)",
                 test_flag);
        selected_resp = s_test_registration_resp;
    }
#endif
    (void)selected_resp;

    ret_status = parse_tms_registration_response(resp_buf, out_resp);
    if (ret_status != 0)
    {
        OSI_LOGE(TAG, "Failed to parse TMS response: %d", ret_status);
        ret_status = -3;
        goto cleanup;
    }

    OSI_LOGI(TAG, "TMS response parsed; response_code=%s",
             out_resp->response_code);
    OSI_LOGI(TAG, "MQTT endpoint: %s:%d (SSL=%d)",
             out_resp->mqtt_ip, out_resp->mqtt_port,
             out_resp->mqtt_ssl_enable);
    ret_status = 0;

cleanup:
    if (resp_buf != NULL)
        free(resp_buf);
    if (hdr_buf != NULL)
        free(hdr_buf);
    return ret_status;
}
int tms_register_with_redirect(tms_registration_resp_t *out_resp)
{
    // Google redirect URLs can be 500-800 bytes; use 1024 buffer
    char current_url[1024];
    strncpy(current_url, TMS_REGISTRATION_URL, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';

    char *resp_buf = malloc(4096);
    char *hdr_buf = malloc(2048); // Large enough for Google's long headers
    if (!resp_buf || !hdr_buf)
    {
        if (resp_buf)
            free(resp_buf);
        if (hdr_buf)
            free(hdr_buf);
        return -1;
    }

    int ret_status = -1;
    int current_method = HTTPCLIENT_POST; // Hop 0 is POST; redirects switch to GET

    for (int hop = 0; hop < MAX_REDIRECT_HOPS; hop++)
    {
        memset(resp_buf, 0, 4096);
        memset(hdr_buf, 0, 2048);

        httpclient_t client = {0};
        httpclient_data_t client_data = {0};

        client.header = "Content-Type: application/json\r\nAccept: text/plain, */*\r\n";
        client.verify_mode = 0; // VERIFY_NONE

        client_data.response_buf = resp_buf;
        client_data.response_buf_len = 4096;
        client_data.header_buf = hdr_buf;
        client_data.header_buf_len = 2048;

        HTTPCLIENT_RESULT http_res;

        if (current_method == HTTPCLIENT_POST)
        {
            // Hop 0: Standard POST headers (NO "Connection: close")
            client.header = "Content-Type: application/json\r\nAccept: text/plain, */*\r\n";
            client_data.post_buf = (char *)s_hardcoded_json_req;
            client_data.post_buf_len = strlen(s_hardcoded_json_req);

            OSI_LOGI(TAG, "Requesting [Hop %d POST]: %s", hop, current_url);
            http_res = httpclient_post(&client, current_url, &client_data);
        }
        else
        {
            // Hop 1 (Redirect): Standard GET headers without body
            client.header = "Accept: text/plain, */*\r\n";
            client_data.post_buf = NULL;
            client_data.post_buf_len = 0;

            OSI_LOGI(TAG, "Requesting [Hop %d GET]: %s", hop, current_url);
            http_res = httpclient_get(&client, current_url, &client_data);
        }

        // 1. Success (HTTP 200 OK)
        if (http_res == HTTPCLIENT_OK && client.response_code == 200)
        {
            OSI_LOGI(TAG, "HTTP 200 received (length=%u)", (unsigned int)strlen(resp_buf));
            log_string_in_chunks_with_label(TAG, "TMS_REDIRECT_RESP", resp_buf);
            ret_status = parse_tms_registration_response(resp_buf, out_resp);
            break;
        }

        // 2. Redirect (HTTP 301 / 302 / 303 / 307)
        if (http_res == HTTPCLIENT_OK &&
            (client.response_code == 301 || client.response_code == 302 ||
             client.response_code == 303 || client.response_code == 307))
        {

            int loc_pos = 0;
            int loc_len = 0;

            if (httpclient_get_response_header_value(hdr_buf, "Location", &loc_pos, &loc_len) == 0 && loc_len > 0)
            {
                if (loc_len < sizeof(current_url))
                {
                    memcpy(current_url, hdr_buf + loc_pos, loc_len);
                    current_url[loc_len] = '\0';
                    OSI_LOGI(TAG, "Redirected (%d) -> %s", client.response_code, current_url);

                    // CRITICAL: Switch method to GET for the next hop!
                    current_method = HTTPCLIENT_GET;
                    continue;
                }
                else
                {
                    OSI_LOGE(TAG, "Redirect URL exceeds buffer length (%d)", loc_len);
                    ret_status = -2;
                    break;
                }
            }
            else
            {
                OSI_LOGE(TAG, "HTTP %d but no Location header found", client.response_code);
                ret_status = -3;
                break;
            }
        }

        // 3. Other errors
        OSI_LOGE(TAG, "HTTP request failed! res=%d, code=%d", http_res, client.response_code);
        ret_status = -4;
        break;
    }

    free(resp_buf);
    free(hdr_buf);
    return ret_status;
}
static unsigned int tms_next_retry_delay(unsigned int current_delay_ms)
{
    if (current_delay_ms >= TMS_RETRY_MAX_DELAY_MS / 2U)
        return TMS_RETRY_MAX_DELAY_MS;
    return current_delay_ms * 2U;
}

static bool tms_response_is_accepted(const tms_registration_resp_t *resp)
{
    if (resp == NULL)
        return false;

    /* Do not treat HTTP 200 or successful parsing as TMS approval. */
    // if (strcmp(resp->response_code,"0000") == 0)
    // {
    //     OSI_LOGE(TAG,
    //              "Set TMS_REGISTRATION_SUCCESS_CODE to the real backend success code");
    //     return false;
    // }

    return strcmp(resp->response_code, TMS_REGISTRATION_SUCCESS_CODE) == 0;
}

/* ============================================================================
 * Registration Response Download Handlers (Only evaluated when code is 9999)
 * ============================================================================ */

/**
 * @brief Handle Welcome (WEL) audio file download (13th & 14th response fields).
 * @param resp Parsed registration response structure.
 * @return true if download was successfully queued/started; false otherwise.
 */
typedef struct
{
    osiMutex_t *mutex;
    bool active;
    bool wel_requested;
    bool wel_done;
    bool wel_ok;
    bool exe_requested;
    bool exe_done;
    bool exe_ok;
    char participant_id[64];
    char welcome_dst[128];
} tms_bg_reg_mgr_t;

static tms_bg_reg_mgr_t s_bg_reg_mgr = {0};

static void prv_bg_reg_check_and_send_ack(void)
{
    if (!s_bg_reg_mgr.mutex)
        return;
    osiMutexLock(s_bg_reg_mgr.mutex);
    if (!s_bg_reg_mgr.active)
    {
        osiMutexUnlock(s_bg_reg_mgr.mutex);
        return;
    }
    if (!s_bg_reg_mgr.wel_done || !s_bg_reg_mgr.exe_done)
    {
        osiMutexUnlock(s_bg_reg_mgr.mutex);
        return;
    }

    s_bg_reg_mgr.active = false;
    bool need_restart = false; /* Continue normal operations without rebooting */
    const char *wel_flag_str = tms_map_ack_status(s_bg_reg_mgr.wel_requested, s_bg_reg_mgr.wel_ok);
    const char *exe_flag_str = tms_map_ack_status(s_bg_reg_mgr.exe_requested, s_bg_reg_mgr.exe_ok);
    char pid[64] = {0};
    strncpy(pid, s_bg_reg_mgr.participant_id, sizeof(pid) - 1);
    if (pid[0] == '\0')
    {
        strncpy(pid, customer_register_get_partner_id(), sizeof(pid) - 1);
    }
    osiMutexUnlock(s_bg_reg_mgr.mutex);

    char sn[32] = {0};
    framework_device_config_get_sn(sn, sizeof(sn));
    if (sn[0] == '\0')
    {
        strncpy(sn, "BP02867772071407197", sizeof(sn) - 1);
    }

    OSI_LOGI(TAG, "Dispatching UPDATESBFLAG ACK: sn=%s, pid=%s, welfileflag=%s, exefileflag=%s (continuing normal operations)",
             sn, pid, wel_flag_str, exe_flag_str);

    int ack_ret = tms_send_update_sb_flag_ack(
        TMS_REGISTRATION_URL,
        sn,
        pid,
        wel_flag_str,
        exe_flag_str,
        need_restart);
    if (ack_ret != 0)
    {
        OSI_LOGE(TAG, "UPDATESBFLAG ACK dispatch failed: %d", ack_ret);
    }
    else
    {
        OSI_LOGI(TAG, "UPDATESBFLAG ACK dispatch queued successfully");
    }
}

static void prv_reg_welcome_dl_callback(uint32_t msg_id, const char *save_path, void *user_data)
{
    (void)user_data;
    s_test_download_in_progress = false;

    if (s_bg_reg_mgr.mutex)
        osiMutexLock(s_bg_reg_mgr.mutex);
    s_bg_reg_mgr.wel_done = true;
    if (msg_id == MSG_HTTP_DOWNLOAD_SUCCESS && fs_size(s_bg_reg_mgr.welcome_dst) > 0)
    {
        OSI_LOGI(TAG, "Background WEL download completed successfully: %s (%u bytes)",
                 save_path ? save_path : "", (unsigned int)fs_size(s_bg_reg_mgr.welcome_dst));
        s_bg_reg_mgr.wel_ok = true;

        /* Verify completed test operation: only persist TEST_FLAG=1 once file is verified on disk */
        int cur_flag = customer_test_get_flag();
        if (cur_flag == 0)
        {
            OSI_LOGI(TAG, "[TEST_FLAG] Welcome audio verified on disk (%u bytes). Updating TEST_FLAG=1",
                     (unsigned int)fs_size(s_bg_reg_mgr.welcome_dst));
            int w_res = customer_test_write_flag(1);
            if (w_res == 0)
            {
                OSI_LOGI(TAG, "[TEST_FLAG] TEST_FLAG=1 persisted successfully. Following boot will use normal response 0000");
            }
            else
            {
                OSI_LOGE(TAG, "[TEST_FLAG] Failed to persist TEST_FLAG=1 (ret=%d); flag remains 0 for retry", w_res);
            }
        }
    }
    else
    {
        OSI_LOGE(TAG, "Background WEL download failed: msg_id=%u, path=%s, size=%u; keeping TEST_FLAG=0 for retry on next boot",
                 (unsigned int)msg_id, save_path ? save_path : "", (unsigned int)fs_size(s_bg_reg_mgr.welcome_dst));
        s_bg_reg_mgr.wel_ok = false;
    }
    if (s_bg_reg_mgr.mutex)
        osiMutexUnlock(s_bg_reg_mgr.mutex);

    prv_bg_reg_check_and_send_ack();
}

static bool customer_register_handle_welcome_download(const tms_registration_resp_t *resp)
{
    if (resp == NULL)
    {
        OSI_LOGE(TAG, "WEL download failed: response structure is NULL");
        return false;
    }

    /* Runtime guard to prevent duplicate test download operations */
    if (s_test_download_in_progress)
    {
        OSI_LOGW(TAG, "WEL download already in progress; skipping duplicate trigger");
        return false;
    }

    /* Check Welcome Audio Flag at 13th response index */
    if (resp->welcome_file_flag != 1)
    {
        OSI_LOGI(TAG, "WEL download skipped: flag is %d (disabled)", resp->welcome_file_flag);
        return false;
    }

    OSI_LOGI(TAG, "WEL download enabled (flag=1)");

    /* Retrieve Welcome file URL from 14th response index */
    const char *url = resp->welcome_file_url;
    if (url[0] == '\0')
    {
        OSI_LOGW(TAG, "WEL download skipped: welcome_file_url is empty");
        return false;
    }

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
    {
        OSI_LOGE(TAG, "WEL download failed: invalid protocol (must begin with http:// or https://)");
        return false;
    }

    /* Extract and parse filename from URL */
    const char *last_slash = strrchr(url, '/');
    if (!last_slash || *(last_slash + 1) == '\0')
    {
        OSI_LOGE(TAG, "WEL download failed: cannot parse filename from URL: %s", url);
        return false;
    }

    char raw_filename[128] = {0};
    const char *src_p = last_slash + 1;
    size_t raw_len = 0;

    /* Strip query parameters and URL delimiters */
    while (*src_p != '\0' && *src_p != '?' && *src_p != '#' && raw_len < (sizeof(raw_filename) - 5))
    {
        raw_filename[raw_len++] = *src_p++;
    }
    raw_filename[raw_len] = '\0';

    if (raw_len == 0)
    {
        OSI_LOGE(TAG, "WEL download failed: extracted empty filename from URL");
        return false;
    }

    /* Preserve exact filename from URL (e.g. id_welcome_to_template.mp3) */
    const char *actual_name = raw_filename;

    char welcome_filename[128] = {0};
    strncpy(welcome_filename, actual_name, sizeof(welcome_filename) - 5);
    welcome_filename[sizeof(welcome_filename) - 5] = '\0';
    size_t fn_len = strlen(welcome_filename);

    /* Ensure destination filename ends in .mp3 */
    if (fn_len < 4 || (strcmp(welcome_filename + fn_len - 4, ".mp3") != 0 &&
                       strcmp(welcome_filename + fn_len - 4, ".MP3") != 0))
    {
        OSI_LOGW(TAG, "Welcome filename '%s' lacks .mp3 extension; appending .mp3", welcome_filename);
        strncat(welcome_filename, ".mp3", sizeof(welcome_filename) - strlen(welcome_filename) - 1);
    }

    /* Retrieve active audio language folder prefix */
    const char *active_folder = audio_resource_get_language_prefix();
    if (!active_folder || active_folder[0] == '\0')
    {
#ifdef AUDIO_LANG_FEMALE_PATH
        active_folder = AUDIO_LANG_FEMALE_PATH;
#else
        active_folder = "D:/FEMALE/";
#endif
    }

    if (s_bg_reg_mgr.mutex)
        osiMutexLock(s_bg_reg_mgr.mutex);
    memset(s_bg_reg_mgr.welcome_dst, 0, sizeof(s_bg_reg_mgr.welcome_dst));
    size_t af_len = strlen(active_folder);
    if (af_len > 0 && active_folder[af_len - 1] == '/')
    {
        snprintf(s_bg_reg_mgr.welcome_dst, sizeof(s_bg_reg_mgr.welcome_dst), "%s%s", active_folder, welcome_filename);
    }
    else
    {
        snprintf(s_bg_reg_mgr.welcome_dst, sizeof(s_bg_reg_mgr.welcome_dst), "%s/%s", active_folder, welcome_filename);
    }
    char dst_copy[128];
    strncpy(dst_copy, s_bg_reg_mgr.welcome_dst, sizeof(dst_copy) - 1);
    dst_copy[sizeof(dst_copy) - 1] = '\0';
    if (s_bg_reg_mgr.mutex)
        osiMutexUnlock(s_bg_reg_mgr.mutex);

    OSI_LOGI(TAG, "Queuing background WEL download: %s -> %s", url, dst_copy);

    s_test_download_in_progress = true;
    int download_ret = framework_http_download_file_callback(
        url,
        dst_copy,
        HTTP_DOWNLOAD_TYPE_FILE,
        prv_reg_welcome_dl_callback,
        NULL);

    if (download_ret != 0)
    {
        s_test_download_in_progress = false;
        OSI_LOGE(TAG, "WEL download failed: error queuing download (ret=%d)", download_ret);
        if (s_bg_reg_mgr.mutex)
            osiMutexLock(s_bg_reg_mgr.mutex);
        s_bg_reg_mgr.wel_done = true;
        s_bg_reg_mgr.wel_ok = false;
        if (s_bg_reg_mgr.mutex)
            osiMutexUnlock(s_bg_reg_mgr.mutex);
        prv_bg_reg_check_and_send_ack();
        return false;
    }

    OSI_LOGI(TAG, "WEL download successfully queued to background http_task");
    return true;
}

/**
 * @brief Handle Application (APP) OTA update download (1st & 8th response fields).
 * @param resp Parsed registration response structure.
 * @return true if download was successfully queued/started; false otherwise.
 */
static bool customer_register_handle_app_download(const tms_registration_resp_t *resp)
{
    if (resp == NULL)
    {
        OSI_LOGE(TAG, "APP download failed: response structure is NULL");
        return false;
    }

    /* Prerequisite check: response index 1 contains 9999 */
    if (strcmp(resp->response_code, "9999") != 0)
    {
        OSI_LOGI(TAG, "APP download skipped: response code '%s' is not 9999", resp->response_code);
        return false;
    }

    OSI_LOGI(TAG, "APP download enabled: response code is 9999");

    /* Retrieve application download URL from response index 8 */
    const char *url = resp->app_download_url;
    if (!url || url[0] == '\0')
    {
        OSI_LOGW(TAG, "APP download skipped: app_download_url is empty");
        return false;
    }

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
    {
        OSI_LOGE(TAG, "APP download failed: invalid protocol (must begin with http:// or https://)");
        return false;
    }

    OSI_LOGI(TAG, "APP download started: %s", url);
    audio_resource_play_combo_async_mid_priority("", "upgrade-start.mp3", NULL, 1);

    cJSON *json_obj = cJSON_CreateObject();
    if (!json_obj)
    {
        OSI_LOGE(TAG, "APP download failed: cJSON allocation error");
        return false;
    }

    cJSON_AddStringToObject(json_obj, "url", url);
    char *json_str = cJSON_PrintUnformatted(json_obj);
    if (!json_str)
    {
        OSI_LOGE(TAG, "APP download failed: JSON serialization error");
        cJSON_Delete(json_obj);
        return false;
    }

    int update_res = customer_app_update_from_json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(json_obj);

    if (update_res != 0)
    {
        OSI_LOGE(TAG, "APP download failed: customer_app_update_from_json returned %d", update_res);
        return false;
    }

    OSI_LOGI(TAG, "APP download queued successfully (ret=0)");
    return true;
}

static void prv_reg_exception_dl_callback(tms_exception_download_status_t status, void *user_data)
{
    (void)user_data;
    if (s_bg_reg_mgr.mutex)
        osiMutexLock(s_bg_reg_mgr.mutex);
    s_bg_reg_mgr.exe_done = true;
    s_bg_reg_mgr.exe_ok = (status == TMS_EXCEPTION_DOWNLOAD_SUCCESS);
    OSI_LOGI(TAG, "Background Exception download finished with status: %d (ok=%d)",
             (int)status, s_bg_reg_mgr.exe_ok);
    if (s_bg_reg_mgr.mutex)
        osiMutexUnlock(s_bg_reg_mgr.mutex);

    prv_bg_reg_check_and_send_ack();
}

/**
 * @brief Handle TMS Exception audio file download (15th & 16th response fields).
 * @param resp Parsed registration response structure.
 * @return true if download was successfully queued; false otherwise.
 */
static bool customer_register_handle_exception_download(const tms_registration_resp_t *resp)
{
    if (resp == NULL)
    {
        OSI_LOGE(TAG, "Exception download failed: response structure is NULL");
        return false;
    }

    /* Check Exception Audio Flag at 15th response index */
    if (resp->exception_file_flag != 1)
    {
        OSI_LOGI(TAG, "Exception download skipped: flag is %d (disabled)", resp->exception_file_flag);
        return false;
    }

    OSI_LOGI(TAG, "Exception download enabled (flag=1)");

    /* Retrieve Exception file URL from response index 16 */
    const char *url = resp->exception_file_url;
    if (!url || url[0] == '\0')
    {
        OSI_LOGW(TAG, "Exception download skipped: exception_file_url is empty");
        return false;
    }

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
    {
        OSI_LOGE(TAG, "Exception download failed: invalid protocol (must begin with http:// or https://)");
        return false;
    }

    OSI_LOGI(TAG, "Queuing background Exception download: %s", url);

    tms_exception_download_set_completion_callback(prv_reg_exception_dl_callback, NULL);

    tms_exception_download_status_t exe_status = tms_exception_download_process(
        resp->exception_file_flag,
        resp->exception_file_url,
        customer_get_default_language());

    if (exe_status != TMS_EXCEPTION_DOWNLOAD_SUCCESS)
    {
        OSI_LOGE(TAG, "Exception download failed to queue: status %d", (int)exe_status);
        if (s_bg_reg_mgr.mutex)
            osiMutexLock(s_bg_reg_mgr.mutex);
        s_bg_reg_mgr.exe_done = true;
        s_bg_reg_mgr.exe_ok = false;
        if (s_bg_reg_mgr.mutex)
            osiMutexUnlock(s_bg_reg_mgr.mutex);
        prv_bg_reg_check_and_send_ack();
        return false;
    }

    OSI_LOGI(TAG, "Exception download successfully queued to background http_task");
    return true;
}

/**
 * @brief Handle TMS Language Change Workflow per DUX TMS Sound Box API Specification V2.1
 * @param resp Parsed registration response structure containing fields 19, 20, 21.
 */
void customer_register_handle_lang_download(const tms_registration_resp_t *resp)
{
    if (resp == NULL)
    {
        return;
    }

    int flag = resp->lang_download_flag;
    OSI_LOGI(TAG, "[LANG] Language change flag: %d", flag);

    if (flag != 1)
    {
        return;
    }

    char num_str[16] = {0};
    snprintf(num_str, sizeof(num_str), "%d", resp->lang_numeric_code);

    soundbox_language_t req_lang = SOUNDBOX_DEFAULT_LANGUAGE;
    bool is_valid = soundbox_str_to_language(num_str, &req_lang);

    soundbox_language_t current_lang = soundbox_get_language();
    const char *current_lang_str = soundbox_language_to_str(current_lang);
    const char *requested_lang_str = is_valid ? soundbox_language_to_str(req_lang) : num_str;
    const char *request_id_str = (resp->change_lang_req_id[0] != '\0') ? resp->change_lang_req_id : "0";

    OSI_LOGI(TAG, "[LANG] Current language: %s", current_lang_str);
    OSI_LOGI(TAG, "[LANG] Requested language: %s", requested_lang_str);
    OSI_LOGI(TAG, "[LANG] Request ID: %s", request_id_str);

    char sn[32] = {0};
    framework_device_config_get_sn(sn, sizeof(sn));
    double req_id = strtod(request_id_str, NULL);

    /* 1. Validate requested language */
    if (!is_valid)
    {
        OSI_LOGE(TAG, "[LANG] Invalid or unsupported requested language code: %s", num_str);
        if (req_id > 0)
        {
            customer_send_change_language_ack(
                req_id,
                sn,
                "Failed",
                "Unsupported language code",
                soundbox_language_to_numeric_str(current_lang));
        }
        return;
    }

    /* 2. Same language - idempotent check (Section 3) */
    if (current_lang == req_lang)
    {
        OSI_LOGI(TAG, "[LANG] Language already active, skipping download");
        if (req_id > 0)
        {
            customer_send_change_language_ack(
                req_id,
                sn,
                " Success ",
                "",
                soundbox_language_to_numeric_str(current_lang));
        }
        return;
    }

    /* 3. Different language - directly start the download workflow */
    OSI_LOGI(TAG, "[LANG] Language mismatch, starting existing DOWNLOADLANG workflow");
    int dl_ret = customer_lang_download_trigger(request_id_str, NULL);
    if (dl_ret != 0)
    {
        OSI_LOGE(TAG, "[LANG] Failed to trigger language download workflow: %d", dl_ret);
        if (req_id > 0)
        {
            customer_send_change_language_ack(
                req_id,
                sn,
                "Failed",
                "Language download trigger failed",
                soundbox_language_to_numeric_str(current_lang));
        }
    }
}

/**
 * @brief Process registration response: queue background downloads and return immediately.
 * @param resp Parsed registration response structure.
 */
void customer_register_process_response(const tms_registration_resp_t *resp)
{
    if (resp == NULL)
    {
        OSI_LOGE(TAG, "Cannot process registration response: response pointer is NULL");
        return;
    }

    OSI_LOGI(TAG, "Registration response received");
    OSI_LOGI(TAG, "Registration response code: %s", resp->response_code);

    /* =========================================================================
     * Response Code 9999: App Update Workflow
     * ========================================================================= */
    if (strcmp(resp->response_code, "9999") == 0)
    {
        OSI_LOGI(TAG, "[9999] Registration response code is 9999: checking App update URL");

        /* Check App update URL (8th response field: app_download_url) */
        if (resp->app_download_url[0] != '\0')
        {
            OSI_LOGI(TAG, "[9999] App update URL found: %s", resp->app_download_url);
            customer_register_handle_app_download(resp);
        }
        else
        {
            OSI_LOGI(TAG, "[9999] App update URL is empty, skipping app update");
        }
    }
    /* =========================================================================
     * Response Code 0000: Registration Success - Welcome Audio, Exception, Lang
     * ========================================================================= */
    else if (strcmp(resp->response_code, "0000") == 0 || strcmp(resp->response_code, TMS_REGISTRATION_SUCCESS_CODE) == 0)
    {
        OSI_LOGI(TAG, "[0000] Registration response code is 0000: processing registration artifacts");

        if (resp->participant_id[0] != '\0')
        {
            strncpy(s_tms_partner_id, resp->participant_id, sizeof(s_tms_partner_id) - 1);
            s_tms_partner_id[sizeof(s_tms_partner_id) - 1] = '\0';
        }
        if (resp->application_code[0] != '\0')
        {
            strncpy(s_tms_app_code, resp->application_code, sizeof(s_tms_app_code) - 1);
            s_tms_app_code[sizeof(s_tms_app_code) - 1] = '\0';
        }

        /* 1. Verify Welcome Audio and Section 8 Soundbox Exception File Download */
        if (!s_bg_reg_mgr.mutex)
        {
            s_bg_reg_mgr.mutex = osiMutexCreate();
        }

        osiMutexLock(s_bg_reg_mgr.mutex);
        s_bg_reg_mgr.wel_requested = (resp->welcome_file_flag == 1);
        s_bg_reg_mgr.wel_done = !s_bg_reg_mgr.wel_requested;
        s_bg_reg_mgr.wel_ok = false;

        s_bg_reg_mgr.exe_requested = (resp->exception_file_flag == 1);
        s_bg_reg_mgr.exe_done = !s_bg_reg_mgr.exe_requested;
        s_bg_reg_mgr.exe_ok = false;

        s_bg_reg_mgr.active = true;

        if (resp->participant_id && resp->participant_id[0] != '\0')
        {
            strncpy(s_bg_reg_mgr.participant_id, resp->participant_id, sizeof(s_bg_reg_mgr.participant_id) - 1);
            s_bg_reg_mgr.participant_id[sizeof(s_bg_reg_mgr.participant_id) - 1] = '\0';
        }
        else
        {
            s_bg_reg_mgr.participant_id[0] = '\0';
        }
        osiMutexUnlock(s_bg_reg_mgr.mutex);

        /* Queue Welcome Audio Download in background HTTP task if requested (13th index == 1) */
        if (resp->welcome_file_flag == 1)
        {
            OSI_LOGI(TAG, "Welcome file flag is 1: queuing welcome audio download");
            customer_register_handle_welcome_download(resp);
        }
        else
        {
            OSI_LOGI(TAG, "Welcome file flag is %d: skipping welcome audio download", resp->welcome_file_flag);
        }

        /* Queue Section 8 Exception File Download in background HTTP task if requested (15th index == 1) */
        if (resp->exception_file_flag == 1)
        {
            OSI_LOGI(TAG, "Exception file flag is 1 (Section 8 Exception API): queuing exception download");
            customer_register_handle_exception_download(resp);
        }
        else
        {
            OSI_LOGI(TAG, "Exception file flag is %d: skipping exception download", resp->exception_file_flag);
        }

        /* If neither was requested (0,0), both wel_done and exe_done are already true; send ACK immediately */
        if (resp->welcome_file_flag != 1 && resp->exception_file_flag != 1)
        {
            OSI_LOGI(TAG, "Both welcome and exception flags are not 1; sending ACK immediately");
            prv_bg_reg_check_and_send_ack();
        }

        /* 2. Check Language download workflow (19th, 20th, 21st response fields) */
        customer_register_handle_lang_download(resp);
    }
    else
    {
        OSI_LOGW(TAG, "Registration response code '%s' is neither 9999 nor 0000; skipping downloads",
                 resp->response_code);
    }
}

static bool s_tms_reg_in_progress = false;

bool customer_register_is_in_progress(void)
{
    return s_tms_reg_in_progress;
}

void customer_register_set_in_progress(bool in_progress)
{
    s_tms_reg_in_progress = in_progress;
}

void tms_registration_task(void *param)
{
    s_tms_reg_in_progress = true;
    unsigned int attempts = 0U;
    unsigned int retry_delay_ms = TMS_RETRY_INITIAL_DELAY_MS;
    static tms_registration_resp_t tms_info;

    (void)param;

    OSI_LOGI(TAG, "TMS registration task started");
    customer_test_init();

    for (;;)
    {
        while (!framework_network_is_pdp_activated())
        {
            int csq = framework_network_get_csq();
            int reg_stat = framework_network_get_lac(); // CEREG status: 1=Home, 5=Roaming, 2=Searching, 0=Not registered
            bool pdp = framework_network_is_pdp_activated();
            OSI_LOGI(TAG, "Waiting for network: Signal(CSQ)=%d, RegStatus=%d, PDPActive=%d",
                     csq, reg_stat, pdp);
            osiThreadSleep(1000);
        }

        attempts++;
        memset(&tms_info, 0, sizeof(tms_info));
        OSI_LOGI(TAG, "TMS registration attempt %u", attempts);

        int res = tms_register_device_sync(&tms_info);
        // int res = tms_register_with_redirect(&tms_info);

        /* 1. Validate registration function result & confirm response was parsed successfully */
        if (tms_info.mqtt_ip[0] != '\0')
        {
            snprintf(mqtt_reg.ip, sizeof(mqtt_reg.ip), "%s", tms_info.mqtt_ip);
            if (tms_info.mqtt_port > 0)
            {
                mqtt_reg.port = tms_info.mqtt_port;
            }
            else
            {
                mqtt_reg.port = 9000;
            }
            snprintf(mqtt_reg.username, sizeof(mqtt_reg.username), "%s", tms_info.mqtt_user);
            snprintf(mqtt_reg.password, sizeof(mqtt_reg.password), "%s", tms_info.mqtt_pass);
            mqtt_reg.ssl_enable = tms_info.mqtt_ssl_enable;
            snprintf(mqtt_reg.partner_id, sizeof(mqtt_reg.partner_id), "%s", tms_info.participant_id);
            if (tms_info.participant_id[0] != '\0')
            {
                strncpy(s_tms_partner_id, tms_info.participant_id, sizeof(s_tms_partner_id) - 1);
            }
            if (tms_info.application_code[0] != '\0')
            {
                strncpy(s_tms_app_code, tms_info.application_code, sizeof(s_tms_app_code) - 1);
            }
            OSI_LOGI(TAG, "Configured mqtt_reg: %s:%d (user=%s, ssl=%d, partner_id=%s)",
                     mqtt_reg.ip, mqtt_reg.port, mqtt_reg.username, mqtt_reg.ssl_enable, mqtt_reg.partner_id);
        }
        else
        {
            snprintf(mqtt_reg.ip, sizeof(mqtt_reg.ip), "%s", "1.7.140.58");
            mqtt_reg.port = 9000;
            mqtt_reg.ssl_enable = 0;
        }

        if (res == 0)
        {
            /* Process registration response (App update if 9999, Welcome/Exception/Lang if 0000) */
            customer_register_process_response(&tms_info);

            if (strcmp(tms_info.response_code, "9999") == 0 || tms_response_is_accepted(&tms_info)||strcmp(tms_info.response_code, "0000") == 0)
            {
                OSI_LOGI(TAG, "TMS REGISTRATION ACCEPTED (response_code=%s) - moving forward to MQTT",
                         tms_info.response_code);
                break;
            }
            else
            {
                OSI_LOGE(TAG, "TMS registration not accepted; response_code=%s",
                         tms_info.response_code);
            }
        }
        else
        {
            OSI_LOGE(TAG, "TMS registration attempt failed; error=%d", res);
        }

        if (TMS_MAX_RETRIES != 0U && attempts >= TMS_MAX_RETRIES)
        {
            OSI_LOGE(TAG, "Maximum TMS registration attempts reached; task stopping");
            s_tms_reg_in_progress = false;
            osiThreadExit();
            return;
        }

        OSI_LOGI(TAG, "Retrying TMS registration in %u ms", retry_delay_ms);
        osiThreadSleep(retry_delay_ms);
        retry_delay_ms = tms_next_retry_delay(retry_delay_ms);
    }

    /* Continue application initialization only after confirmed TMS approval. */
    OSI_LOGI(TAG, "TMS registration completed; MQTT endpoint=%s:%d",
             tms_info.mqtt_ip, tms_info.mqtt_port);

    if (strcmp(tms_info.response_code, TMS_REGISTRATION_SUCCESS_CODE) == 0 || strcmp(tms_info.response_code, "9999") == 0)
    {
        /* Initialize MQTT handler using TMS info */
        customer_mqtt_network_ready();
        OSI_LOGI(TAG, "Initializing MQTT handler from TMS registration");
        int ret = customer_mqtt_init();
        if (ret != 0)
        {
            OSI_LOGE(TAG, "Failed to initialize MQTT handler: %d", ret);
        }
        else
        {
            OSI_LOGI(TAG, "MQTT handler initialized successfully");
        }
    }
    else
    {
        OSI_LOGE(TAG, "TMS Registration failed with code: %s", tms_info.response_code);
        audio_resource_play_combo_async_mid_priority("", "server-failed.mp3", NULL, 1);
    }

    s_tms_reg_in_progress = false;
    osiThreadExit();
}
