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
 * download, updates language, plays the audio, and reboots.
 *
 * @param url   URL pointing to the audio file
 * @param dst   Target file path (or NULL for auto-naming)
 * @param lang  Language name/code to apply after download
 * @return 0 on success, negative error code on failure
 */
int customer_audio_download_handle_single_mp3(const char *url, const char *dst, const char *lang);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_AUDIO_DOWNLOAD_H_ */
