/**
 * @file customer_lang_download.h
 * @brief TMS Language Pack Download Manager Header
 * 
 * Implements complete workflow:
 * - MQTT trigger: DOWNLOADLANG|<request_id>|<url> (or DOWNLOADLANG|<request_id>)
 * - Metadata fetching (TXT/JSON file or GetLanguageFileDetails API)
 * - Sequential chunk/file ZIP downloading with retry
 * - SHA-256 verification (mbedTLS)
 * - Safe extraction into staging and audio verification
 * - Safe atomic language activation and persistence
 * - DownloadLanguageacknowledgement API dispatch
 */

#ifndef _CUSTOMER_LANG_DOWNLOAD_H_
#define _CUSTOMER_LANG_DOWNLOAD_H_

#include <stdbool.h>
#include <stdint.h>
#include "framework_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the language download subsystem
 * @return 0 on success, negative on error
 */
int customer_lang_download_init(void);

/**
 * @brief Handle MQTT command string starting with "DOWNLOADLANG|"
 * 
 * Safely parses pipe tokens:
 * - Token 0: DOWNLOADLANG
 * - Token 1: request_id (e.g. 202540563751)
 * - Token 2: optional metadata TXT/JSON URL
 * 
 * @param payload Full MQTT message payload
 * @return 0 on success (queued to worker), negative on validation/busy error
 */
int customer_lang_download_handle_mqtt_command(const char *payload);

/**
 * @brief Manually trigger language pack download workflow
 * 
 * @param request_id_str Request ID string (e.g. "202540563751")
 * @param metadata_url Optional metadata URL (NULL for production API)
 * @return 0 on success, negative on error
 */
int customer_lang_download_trigger(const char *request_id_str, const char *metadata_url);

/**
 * @brief Check if language download workflow is currently active
 * @return true if running, false if idle
 */
bool customer_lang_download_is_busy(void);

/**
 * @brief Handler for framework unzip messages (MSG_UNZIP_SUCCESS, MSG_UNZIP_FAIL)
 * @param msg Pointer to framework message
 */
void customer_lang_download_handle_unzip_msg(framework_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_LANG_DOWNLOAD_H_ */
