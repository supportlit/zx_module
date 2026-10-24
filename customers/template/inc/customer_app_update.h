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
 * @brief Parse JSON payload directly and trigger app / firmware update
 *
 * Extracts "url" and "type" from the cJSON object and invokes customer_app_update_start().
 *
 * @param root Parsed cJSON object containing "url" and optional "type"
 * @return 0 on success, negative error code on failure
 */
int customer_app_update_from_json(const cJSON *root);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_APP_UPDATE_H_ */
