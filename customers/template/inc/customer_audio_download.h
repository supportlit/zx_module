#ifndef _CUSTOMER_AUDIO_DOWNLOAD_H_
#define _CUSTOMER_AUDIO_DOWNLOAD_H_

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle audio download request from parsed cJSON object
 *
 * Expected format: {"download": "true", "url": "...", "dst": "...", "language": "..."}
 *
 * @param root Parsed cJSON object
 * @return 0 on success, negative error code on failure
 */
int customer_audio_download_from_json(const cJSON *root);

/**
 * @brief Handle audio download request from JSON string
 *
 * @param json_str Serialized JSON payload
 * @return 0 on success, negative error code on failure
 */
int customer_audio_download_from_json_str(const char *json_str);

/**
 * @brief Dispatch audio download by auto-detecting file type (single file or zip package)
 *
 * @param url              Audio file URL
 * @param dst              Destination path (or NULL for default)
 * @param lang             Language code/name (or NULL)
 * @param should_download  Whether download flag is confirmed
 * @return 0 on success, negative error code on failure
 */
int customer_audio_download_start(const char *url, const char *dst,
                                  const char *lang, bool should_download);

/**
 * @brief Handle downloading and deploying a ZIP audio package
 *
 * @param url   URL pointing to the zip archive
 * @param dst   Target directory (defaults to "D:/FEMALE/" if empty or zip name)
 * @param lang  Language name/code to apply on successful extraction
 * @return 0 on success, negative error code on failure
 */
int customer_audio_download_handle_zip(const char *url, const char *dst, const char *lang);

/**
 * @brief Handle downloading a single audio file (e.g. .mp3)
 *
 * Resolves destination filename, creates directories, starts background HTTP
 * download, updates language, plays the audio, and reboots if requested.
 *
 * @param url                    URL pointing to the audio file
 * @param dst                    Target file path (or NULL for auto-naming)
 * @param lang                   Language name/code to apply after download
 * @param restart_after_download Whether to restart after this download completes
 * @return 0 on success, negative error code on failure
 */
int customer_audio_download_handle_single_mp3(const char *url, const char *dst,
                                              const char *lang, bool restart_after_download);

/**
 * @brief Initialize audio download module resources
 */
void customer_audio_download_init(void);

/**
 * @brief Start a new batch of audio downloads for tracking
 *
 * @param total_required Total number of files required in this batch
 * @return Monotonically increasing batch ID (> 0), or 0 on error
 */
uint32_t customer_audio_batch_start(int total_required);

/**
 * @brief Mark that all download requests for the batch have been scheduled
 *
 * @param batch_id Batch ID returned from customer_audio_batch_start
 */
void customer_audio_batch_finish_scheduling(uint32_t batch_id);

/**
 * @brief Record a failure to queue or an entry error in the batch
 *
 * @param batch_id Batch ID
 */
void customer_audio_batch_record_queue_failure(uint32_t batch_id);

/**
 * @brief Get the currently active batch ID (or 0 if none)
 *
 * @return Active batch ID
 */
uint32_t customer_audio_batch_get_active_id(void);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_AUDIO_DOWNLOAD_H_ */
