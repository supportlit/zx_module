#ifndef _CUSTOMER_APP_UPDATE_H_
#define _CUSTOMER_APP_UPDATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start app / firmware update from parsed parameters
 *
 * Checks if the update is for WiFi firmware or App OTA, determines file format
 * (ZIP archive or raw binary), initiates HTTP download, and triggers audio prompts.
 *
 * @param url   Download URL for firmware/app/archive
 * @param type  Type string (e.g. "wifi-firmware" or NULL/default for application update)
 * @return 0 on success, negative error code on failure
 */
int customer_app_update_start(const char *url, const char *type);

/**
 * @brief Parse JSON string payload and trigger app / firmware update
 *
 * Extracts "url" and optional "type" from the JSON string and invokes customer_app_update_start().
 *
 * @param json_str Serialized JSON string containing "url" and optional "type"
 * @return 0 on success, negative error code on failure
 */
int customer_app_update_from_json(const char *json_str);

/**
 * @brief Parse cJSON object directly and trigger app / firmware update
 *
 * @param root Parsed cJSON object containing "url" and optional "type"
 * @return 0 on success, negative error code on failure
 */
int customer_app_update_from_cjson(const cJSON *root);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_APP_UPDATE_H_ */
