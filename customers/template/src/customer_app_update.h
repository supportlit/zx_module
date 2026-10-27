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

/**
 * @brief Initialize secure OTA update module and worker task
 * @return 0 on success, negative on error
 */
int customer_app_update_init(void);

/**
 * @brief Handle pipe-separated 'ota-update | <url> | <sha256>' MQTT command
 * @details Validates fields, checks sha256 format, enqueues to worker task
 * @param payload Raw payload string
 * @return 0 on success, negative on validation/busy error
 */
int customer_app_update_handle_mqtt_command(const char *payload);

/**
 * @brief Start secure application OTA update with SHA-256 verification
 * @param url Download URL for customer_app.bin
 * @param expected_sha256 64-char hexadecimal SHA-256 checksum string
 * @return 0 on success, negative on error
 */
int customer_app_update_start_secure(const char *url, const char *expected_sha256);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_APP_UPDATE_H_ */
