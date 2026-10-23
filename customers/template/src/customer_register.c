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
#include "audio_resource_map.h"
#include "customer_mqtt_handler.h"
#include "customer_status_report.h"
#include "osi_api.h"
#define MAX_REDIRECT_HOPS 3
#define TAG "TMS_REG"
#define TMS_REGISTRATION_URL \
    "https://1.7.140.58/pos/TMS/V1/SoundBoxManagement/SBLogin"
// #define TMS_REGISTRATION_URL \
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
    "\"serial_no\":\"00001504QR70W031184\","
    "\"app_version\":\"1.0.0.3\","
    "\"audio_version\":\"MP3_V1.0_20221116\","
    "\"TMSIPPort\":\"https://1.7.140.58/hmp\","
    "\"IMEI\":\"867772071407197\","
    "\"SIMNo\":\"8991200050051682685F\","
    "\"firmware_version\":\"v1.2.0\","
    "\"active_language\":\"1\""
    "}";

/* Optional pipe-separated request body retained for backend compatibility. */
static const char *s_hardcoded_pipe_resp =
    "0000|1.7.140.58|9000|admin|Pine@987|0|ABCD||"
    "b15eeee6-b841-44af-9448-2c58d2cadb11|HPRCEKSPFI|"
    "0|0|0| |0| |1|0|0|0|0|";

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
    OSI_LOGI(TAG, "Response is %s", raw_data);
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

    client_data.post_buf = (char *)s_hardcoded_json_req;
    client_data.post_buf_len = strlen(s_hardcoded_json_req);
    client_data.response_buf = resp_buf;
    client_data.response_buf_len = TMS_RESPONSE_BUFFER_SIZE - 1U;

    OSI_LOGI(TAG, "Starting TMS registration POST");
    OSI_LOGI(TAG, "Registration endpoint: %s", TMS_REGISTRATION_URL);
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

    ret_status = parse_tms_registration_response(s_hardcoded_pipe_resp, out_resp);
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
            OSI_LOGI(TAG, "HTTP 200 received! Response: %s", resp_buf);
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

void tms_registration_task(void *param)
{
    unsigned int attempts = 0U;
    unsigned int retry_delay_ms = TMS_RETRY_INITIAL_DELAY_MS;
    static tms_registration_resp_t tms_info;

    (void)param;

    OSI_LOGI(TAG, "TMS registration task started");

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

        {
            int res = tms_register_device_sync(&tms_info);
            // int res = tms_register_with_redirect(&tms_info);
            if (res == 0 && tms_response_is_accepted(&tms_info))
            {
                OSI_LOGI(TAG, "TMS REGISTRATION ACCEPTED");
                break;
            }

            if (res == 0)
            {
                OSI_LOGE(TAG, "TMS registration not accepted; response_code=%s",
                         tms_info.response_code);
            }
            else
            {
                OSI_LOGE(TAG, "TMS registration attempt failed; error=%d", res);
            }
        }

        if (TMS_MAX_RETRIES != 0U && attempts >= TMS_MAX_RETRIES)
        {
            OSI_LOGE(TAG, "Maximum TMS registration attempts reached; task stopping");
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

    if (strcmp(tms_info.response_code, TMS_REGISTRATION_SUCCESS_CODE) == 0)
    {
        if (strlen(tms_info.app_download_url) > 0)
        {
            OSI_LOGI(TAG, "App download URL provided: %s", tms_info.app_download_url);
            /* Trigger OTA only after validating URL and update policy. */
        }

        if (tms_info.welcome_file_flag == 1 &&
            strlen(tms_info.welcome_file_url) > 0)
        {
            OSI_LOGI(TAG, "Welcome audio URL provided: %s",
                     tms_info.welcome_file_url);
            /* Trigger audio download through your existing framework API. */
        }

        /* Initialize MQTT handler using TMS info */
        customer_mqtt_network_ready();
        OSI_LOGI(TAG, "Initializing MQTT handler from TMS registration");
        int ret = customer_mqtt_init();
        if (ret != 0) {
            OSI_LOGE(TAG, "Failed to initialize MQTT handler: %d", ret);
        } else {
            OSI_LOGI(TAG, "MQTT handler initialized successfully");
        }

// #if defined(TMS_HEARTBEAT_ENABLE) && (TMS_HEARTBEAT_ENABLE == 1)
//         customer_tms_heartbeat_start(TMS_HEARTBEAT_INTERVAL_SEC);
// #endif
    }
    else if (strcmp(tms_info.response_code, "9999") == 0)
    {
        OSI_LOGI(TAG, "TMS Update Available (9999): %s", tms_info.app_download_url);
        audio_resource_play_combo_async_mid_priority("", "upgrade-start.mp3", NULL, 1);
    }
    else
    {
        OSI_LOGE(TAG, "TMS Registration failed with code: %s", tms_info.response_code);
        audio_resource_play_combo_async_mid_priority("", "server-failed.mp3", NULL, 1);
    }

    osiThreadExit();
}