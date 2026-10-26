/**
 * @file TmsExceptionDownload.h
 * @brief TMS Exception Audio & File Download Workflow and UPDATESBFLAG ACK Handler
 */

#ifndef _TMS_EXCEPTION_DOWNLOAD_H_
#define _TMS_EXCEPTION_DOWNLOAD_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of the exception file download workflow
 */
typedef enum {
    TMS_EXCEPTION_DOWNLOAD_SKIPPED = 0, /**< Skipped because exception flag != 1 */
    TMS_EXCEPTION_DOWNLOAD_SUCCESS = 1, /**< All manifest entries queued/downloaded successfully */
    TMS_EXCEPTION_DOWNLOAD_FAILED  = -1 /**< Failed due to network, parse, or download error */
} tms_exception_download_status_t;

/**
 * @brief TMS UPDATESBFLAG Status Mapping Configuration
 *
 * Backend API Ambiguity Note:
 * Some TMS backend specifications define "0" as success (clearing the pending flag
 * on the server), while other backends define "1" as success.
 * These macros provide a centralized, configurable mapping:
 */
#define TMS_ACK_FLAG_SUCCESS  "0"
#define TMS_ACK_FLAG_FAILED   "1"
#define TMS_ACK_FLAG_SKIPPED  "0"

/**
 * @brief Run the complete TMS exception-file download workflow.
 *
 * 1. Checks exception_flag; if != 1, returns TMS_EXCEPTION_DOWNLOAD_SKIPPED.
 * 2. Fetches the initial manifest file via HTTP GET (e.g. ..._Exception.txt).
 * 3. Extracts the GEN.txt manifest URL from the response.
 * 4. Fetches and parses GEN.txt line-by-line (<download_url>|<destination_path>).
 * 5. Calls customer_audio_download_handle_single_mp3(url, dst, lang, false) for each entry.
 *
 * @param exception_flag 15th field from TMS registration (1 = available, 0 = skip)
 * @param manifest_url   16th field from TMS registration (initial exception manifest URL)
 * @param default_lang   Active language code (e.g. from customer_get_default_language())
 * @return TMS_EXCEPTION_DOWNLOAD_SUCCESS on full success,
 *         TMS_EXCEPTION_DOWNLOAD_SKIPPED if flag != 1,
 *         TMS_EXCEPTION_DOWNLOAD_FAILED if any manifest or download step fails.
 */
tms_exception_download_status_t tms_exception_download_process(
    int exception_flag,
    const char *manifest_url,
    const char *default_lang
);

/**
 * @brief Send the consolidated TMS UPDATESBFLAG acknowledgement POST request.
 *
 * Endpoint: <Base URL>/TMS/V1/SoundBoxApp/UPDATESBFLAG
 * Request Payload:
 * {
 *   "serial_no": "<serial_no>",
 *   "participantId": "<participantId>",
 *   "welfileflag": "<welfileflag>",
 *   "exefileflag": "<exefileflag>"
 * }
 *
 * @param base_registration_url Base registration URL (e.g. TMS_REGISTRATION_URL)
 * @param serial_no             Device serial number (or NULL to auto-fetch from device config)
 * @param participant_id        Participant ID from registration response (9th field)
 * @param wel_status            Result string for welcome audio (e.g. TMS_ACK_FLAG_SUCCESS)
 * @param exe_status            Result string for exception files (e.g. TMS_ACK_FLAG_SUCCESS)
 * @return 0 on success, negative error code on failure.
 */
int tms_send_update_sb_flag_ack(
    const char *base_registration_url,
    const char *serial_no,
    const char *participant_id,
    const char *wel_status,
    const char *exe_status
);

/**
 * @brief Map boolean success/failure to the configured ACK flag string.
 *
 * @param requested True if the download was requested (flag == 1)
 * @param success   True if the download workflow completed successfully
 * @return Configured flag string ("0" or "1")
 */
const char *tms_map_ack_status(bool requested, bool success);

#ifdef __cplusplus
}
#endif

#endif /* _TMS_EXCEPTION_DOWNLOAD_H_ */
