#include <string.h>
#include "framework_http.h"
#include "framework_log.h"
#include "cJSON.h"
#include "customer_heartbeat_api.h"
#define TAG "SB_HEALTH"
#define HEARTBEAT_URL \
    "https://1.7.140.58/pos/TMS/V1/SoundBoxManagement/SBHealthStatus"
/**
 * @brief Callback function triggered when HTTP response is received
 */
static void health_status_http_callback(http_response_t *response, void *user_data)
{
    if (response == NULL)
    {
        OSI_LOGE(TAG, "Empty HTTP response structure");
        return;
    }

    OSI_LOGI(TAG, "Health Status HTTP Status: %d, Result: %d",
             response->status_code, response->result);

    if (response->result == HTTP_RESULT_SUCCESS && response->status_code == 200)
    {
        OSI_LOGI(TAG, "Raw Response: %s", response->response_data);

        /* Parse JSON response: {"ResponseCode": "00", "ResponseMessage": "Success"} */
        cJSON *root = cJSON_Parse(response->response_data);
        if (root)
        {
            cJSON *code = cJSON_GetObjectItem(root, "ResponseCode");
            cJSON *msg = cJSON_GetObjectItem(root, "ResponseMessage");

            if (code && cJSON_IsString(code))
            {
                OSI_LOGI(TAG, "Server ResponseCode: %s", code->valuestring);
            }
            if (msg && cJSON_IsString(msg))
            {
                OSI_LOGI(TAG, "Server ResponseMessage: %s", msg->valuestring);
            }

            cJSON_Delete(root);
        }
        else
        {
            OSI_LOGE(TAG, "Failed to parse JSON response");
        }
    }
    else
    {
        OSI_LOGE(TAG, "Health status push failed! HTTP Code: %d, Result: %d",
                 response->status_code, response->result);
    }
}
int prv_send_json(cJSON *json_body)
{
    if (json_body == NULL)
    {
        OSI_LOGE(TAG, "prv_send_json: json_body is NULL");
        return -1;
    }

    /* Convert cJSON structure into compact JSON string */
    char *payload_str = cJSON_PrintUnformatted(json_body);
    if (payload_str == NULL)
    {
        OSI_LOGE(TAG, "prv_send_json: Failed to print JSON");
        return -2;
    }

    OSI_LOGI(TAG, "Posting health status to %s (length=%u)", HEARTBEAT_URL, (unsigned int)strlen(payload_str));
    log_string_in_chunks_with_label(TAG, "HEARTBEAT_PAYLOAD", payload_str);

    /* framework_http_post_json makes its own internal copy of payload_str */
    int ret = framework_http_post_json(HEARTBEAT_URL, payload_str, NULL, health_status_http_callback, NULL);

    /* Must free the string allocated by cJSON_PrintUnformatted to prevent memory leak */
    cJSON_free(payload_str);

    if (ret != 0)
    {
        OSI_LOGE(TAG, "prv_send_json: Failed to enqueue HTTP POST: %d", ret);
        return ret;
    }

    return 0;
}
