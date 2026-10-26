/**
 * @file soundbox_language.h
 * @brief Soundbox Language Management and Audio Selection
 */

#ifndef _SOUNDBOX_LANGUAGE_H_
#define _SOUNDBOX_LANGUAGE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEMPLATE_LANGUAGE_FILE_PATH     "D:/language.txt"
#define TEMPLATE_LANGUAGE_FILE_FALLBACK "language.txt"

#define ENGLISH_AUDIO_FOLDER "D:/ENG/"
#define HINDI_AUDIO_FOLDER   "D:/HI/"

/**
 * @brief Supported Soundbox languages
 */
typedef enum
{
    LANGUAGE_ENGLISH  = 0,
    LANGUAGE_HINDI    = 1,
    LANGUAGE_MARATHI  = 3,
    LANGUAGE_GUJARATI = 4,
    LANGUAGE_TAMIL    = 7
} soundbox_language_t;

/**
 * @brief Load language setting from TEMPLATE_LANGUAGE_FILE_PATH (D:/language.txt)
 *
 * @param language Output pointer to receive the validated language enum (or fallback)
 * @return 0 on success, negative error code on failure/fallback
 */
int soundbox_load_language(soundbox_language_t *language);

/**
 * @brief Get the currently active soundbox language enum
 *
 * @return Active soundbox_language_t
 */
soundbox_language_t soundbox_get_language(void);

/**
 * @brief Set the active soundbox language and apply audio folder prefix
 *
 * @param lang Language enum to set
 * @param save_to_file If true, persist numeric code to D:/language.txt
 * @return 0 on success, negative error code if invalid or folder not configured
 */
int soundbox_set_language(soundbox_language_t lang, bool save_to_file);

/**
 * @brief Get audio folder path for a given language
 *
 * @param lang Language enum
 * @return Pointer to folder string (e.g. "D:/ENG/"), or NULL if not configured
 */
const char *soundbox_get_audio_folder(soundbox_language_t lang);

/**
 * @brief Get current audio folder path for active language
 *
 * @return Active audio folder path (e.g. "D:/ENG/" or "D:/HI/")
 */
const char *soundbox_get_current_audio_folder(void);

/**
 * @brief Convert soundbox_language_t to 2-letter string code ("EN", "HI", etc.)
 *
 * @param lang Language enum
 * @return String code
 */
const char *soundbox_language_to_str(soundbox_language_t lang);

/**
 * @brief Convert soundbox_language_t to numeric string code ("0", "1", etc.)
 *
 * @param lang Language enum
 * @return Numeric string code
 */
const char *soundbox_language_to_numeric_str(soundbox_language_t lang);

/**
 * @brief Get English descriptive name for language
 *
 * @param lang Language enum
 * @return Human readable language name
 */
const char *soundbox_language_name(soundbox_language_t lang);

/**
 * @brief Parse string ("0", "1", "EN", "HI", etc.) into soundbox_language_t
 *
 * @param str Input string
 * @param out_lang Output language enum pointer
 * @return true if valid and recognized, false otherwise
 */
bool soundbox_str_to_language(const char *str, soundbox_language_t *out_lang);

/**
 * @brief Initialize language subsystem on Soundbox startup
 */
void soundbox_language_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _SOUNDBOX_LANGUAGE_H_ */
