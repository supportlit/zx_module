#include "customer_language_ack.h"
#include "framework_http.h"
#include "framework_log.h"
#include "cJSON.h"
#include "osi_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "LANG_ACK"

#define LANGUAGE_ACK_URL \
    "https://1.7.140.58/pos/TMS/LanguageFile/DownloadLanguageacknowledgement"

#define SAMPLE_REQUEST_ID  202540563751.0
#define SAMPLE_UUID        "202503240932385882"
#define SAMPLE_DEVICE_ID   "00001504QR70W031163"

/**
 * @brief Callback function triggered when HTTP response is received from TMS
 */
static void language_ack_http_callback(http_response_t *response, void *user_data)
{
    if (response == NULL)
    {
        OSI_LOGE(TAG, "[LANG-ACK] Empty HTTP response structure");
        return;
    }

    OSI_LOGI(TAG, "[LANG-ACK] HTTP Status: %d, Result: %d",
             response->status_code, response->result);

    if (response->result == HTTP_RESULT_SUCCESS && response->status_code == 200)
    {
        OSI_LOGI(TAG, "[LANG-ACK] Raw Response: %s",
                 response->response_data ? response->response_data : "(empty)");

        /* Parse JSON response:
         * {
         *     "RspCode": null,
         *     "RspMessage": null,
         *     "Data": null,
         *     "JData": null,
         *     "ResponseCode": "00",
         *     "ResponseMessage": "Success"
         * }
         */
        cJSON *root = cJSON_Parse(response->response_data);
        if (root)
        {
            cJSON *resp_code = cJSON_GetObjectItem(root, "ResponseCode");
            cJSON *resp_msg = cJSON_GetObjectItem(root, "ResponseMessage");

            if (resp_code && cJSON_IsString(resp_code))
            {
                OSI_LOGI(TAG, "[LANG-ACK] Server ResponseCode: %s", resp_code->valuestring);
            }
            if (resp_msg && cJSON_IsString(resp_msg))
            {
                OSI_LOGI(TAG, "[LANG-ACK] Server ResponseMessage: %s", resp_msg->valuestring);
            }

            cJSON_Delete(root);
        }
        else
        {
            OSI_LOGE(TAG, "[LANG-ACK] Failed to parse JSON response");
        }
    }
    else
    {
        OSI_LOGE(TAG, "[LANG-ACK] Push failed! HTTP Code: %d, Result: %d",
                 response->status_code, response->result);
    }
}

int customer_send_language_ack(double request_id, const char *uuid, const char *device_id,
                               const char *status, const char *reason)
{
    cJSON *json = cJSON_CreateObject();
    if (!json)
    {
        OSI_LOGE(TAG, "[LANG-ACK] Failed to create JSON object");
        return -1;
    }

    // request_id: if <= 0 use sample default 202540563751
    double req_id = (request_id > 0) ? request_id : SAMPLE_REQUEST_ID;
    cJSON_AddNumberToObject(json, "request_id", req_id);

    // uuid: if NULL or empty use sample default "202503240932385882"
    const char *uuid_val = (uuid && uuid[0] != '\0') ? uuid : SAMPLE_UUID;
    cJSON_AddStringToObject(json, "uuid", uuid_val);

    // deviceId: if NULL or empty use sample default "00001504QR70W031163"
    const char *dev_id_val = (device_id && device_id[0] != '\0') ? device_id : SAMPLE_DEVICE_ID;
    cJSON_AddStringToObject(json, "deviceId", dev_id_val);

    // status: "Success" or "Failed"
    const char *status_val = (status && status[0] != '\0') ? status : "Success";
    cJSON_AddStringToObject(json, "status", status_val);

    // reason: "" on success, or error detail on failure
    const char *reason_val = (reason != NULL) ? reason : "";
    cJSON_AddStringToObject(json, "reason", reason_val);

    char *payload_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (payload_str == NULL)
    {
        OSI_LOGE(TAG, "[LANG-ACK] Failed to print JSON");
        return -2;
    }

    OSI_LOGI(TAG, "[LANG-ACK] Sending acknowledgement to %s (length=%u)", LANGUAGE_ACK_URL, (unsigned int)strlen(payload_str));
    log_string_in_chunks_with_label(TAG, "LANG_ACK_PAYLOAD", payload_str);

    int ret = framework_http_post_json(LANGUAGE_ACK_URL, payload_str, NULL, language_ack_http_callback, NULL);
    cJSON_free(payload_str);

    if (ret != 0)
    {
        OSI_LOGE(TAG, "[LANG-ACK] Failed to enqueue HTTP POST: %d", ret);
        return ret;
    }

    return 0;
}

int customer_send_language_ack_sync(double request_id, const char *uuid, const char *device_id,
                                    const char *status, const char *reason, uint32_t timeout_ms)
{
    (void)timeout_ms;
    /* Forward directly to asynchronous ACK to avoid blocking and prevent dangling stack pointer/semaphore crashes */
    return customer_send_language_ack(request_id, uuid, device_id, status, reason);
}
