/**
 * @file soundbox_language.c
 * @brief Soundbox Language Loading, Tracking, and Audio Folder Mapping
 */

#include "soundbox_language.h"
#include "framework_audio.h"
#include "framework_log.h"
#include "fs_api.h"
#include "cJSON.h"
#include "osi_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define TAG "SB_LANG"

static soundbox_language_t s_active_language = SOUNDBOX_DEFAULT_LANGUAGE;
static bool s_soundbox_lang_initialized = false;
static osiMutex_t *s_lang_mutex = NULL;

static void prv_lang_lock(void)
{
    if (!s_lang_mutex) {
        s_lang_mutex = osiMutexCreate();
    }
    if (s_lang_mutex) {
        osiMutexLock(s_lang_mutex);
    }
}

static void prv_lang_unlock(void)
{
    if (s_lang_mutex) {
        osiMutexUnlock(s_lang_mutex);
    }
}

static char *prv_str_trim(char *str)
{
    if (!str) {
        return "";
    }
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
        str++;
    }
    if (*str == '\0') {
        return str;
    }
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

static int prv_strcasecmp(const char *s1, const char *s2)
{
    if (!s1 || !s2) {
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    }
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

const char *soundbox_get_audio_folder(soundbox_language_t lang)
{
    switch (lang) {
        case LANGUAGE_ENGLISH:
            return ENGLISH_AUDIO_FOLDER;

        case LANGUAGE_HINDI:
            return HINDI_AUDIO_FOLDER;

        case LANGUAGE_MARATHI:
            OSI_LOGW(TAG, "[LANG] Marathi (code 3) audio folder is not configured in firmware resources");
            return NULL;

        case LANGUAGE_GUJARATI:
            OSI_LOGW(TAG, "[LANG] Gujarati (code 4) audio folder is not configured in firmware resources");
            return NULL;

        case LANGUAGE_TAMIL:
            OSI_LOGW(TAG, "[LANG] Tamil (code 7) audio folder is not configured in firmware resources");
            return NULL;

        default:
            OSI_LOGE(TAG, "[LANG] Unknown language enum: %d", (int)lang);
            return NULL;
    }
}

const char *soundbox_language_to_str(soundbox_language_t lang)
{
    switch (lang) {
        case LANGUAGE_ENGLISH:  return "EN";
        case LANGUAGE_HINDI:    return "HI";
        case LANGUAGE_MARATHI:  return "MR";
        case LANGUAGE_GUJARATI: return "GJ";
        case LANGUAGE_TAMIL:    return "TA";
        default:                return "EN";
    }
}

const char *soundbox_language_to_numeric_str(soundbox_language_t lang)
{
    switch (lang) {
        case LANGUAGE_ENGLISH:  return "0";
        case LANGUAGE_HINDI:    return "1";
        case LANGUAGE_MARATHI:  return "3";
        case LANGUAGE_GUJARATI: return "4";
        case LANGUAGE_TAMIL:    return "7";
        default:                return "0";
    }
}

const char *soundbox_language_name(soundbox_language_t lang)
{
    switch (lang) {
        case LANGUAGE_ENGLISH:  return "English";
        case LANGUAGE_HINDI:    return "Hindi";
        case LANGUAGE_MARATHI:  return "Marathi";
        case LANGUAGE_GUJARATI: return "Gujarati";
        case LANGUAGE_TAMIL:    return "Tamil";
        default:                return "Unknown";
    }
}

bool soundbox_str_to_language(const char *str, soundbox_language_t *out_lang)
{
    if (!str || !out_lang) {
        return false;
    }

    // Skip leading whitespace
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
        str++;
    }
    if (*str == '\0') {
        return false;
    }

    // Exact matches for string and numeric codes
    if (strcmp(str, "0") == 0 || prv_strcasecmp(str, "EN") == 0 || prv_strcasecmp(str, "ENGLISH") == 0) {
        *out_lang = LANGUAGE_ENGLISH;
        return true;
    }
    if (strcmp(str, "1") == 0 || prv_strcasecmp(str, "HI") == 0 || prv_strcasecmp(str, "HINDI") == 0) {
        *out_lang = LANGUAGE_HINDI;
        return true;
    }
    if (strcmp(str, "3") == 0 || prv_strcasecmp(str, "MR") == 0 || prv_strcasecmp(str, "MARATHI") == 0) {
        *out_lang = LANGUAGE_MARATHI;
        return true;
    }
    if (strcmp(str, "4") == 0 || prv_strcasecmp(str, "GJ") == 0 || prv_strcasecmp(str, "GUJARATI") == 0) {
        *out_lang = LANGUAGE_GUJARATI;
        return true;
    }
    if (strcmp(str, "7") == 0 || prv_strcasecmp(str, "TA") == 0 || prv_strcasecmp(str, "TAMIL") == 0) {
        *out_lang = LANGUAGE_TAMIL;
        return true;
    }

    // Check integer conversion (handles trailing whitespace or newline)
    char *endptr = NULL;
    long val = strtol(str, &endptr, 10);
    if (endptr && endptr != str) {
        while (*endptr == ' ' || *endptr == '\t' || *endptr == '\r' || *endptr == '\n') {
            endptr++;
        }
        if (*endptr == '\0') {
            if (val == 0) { *out_lang = LANGUAGE_ENGLISH;  return true; }
            if (val == 1) { *out_lang = LANGUAGE_HINDI;    return true; }
            if (val == 3) { *out_lang = LANGUAGE_MARATHI;  return true; }
            if (val == 4) { *out_lang = LANGUAGE_GUJARATI; return true; }
            if (val == 7) { *out_lang = LANGUAGE_TAMIL;    return true; }
        }
    }

    return false;
}

int soundbox_ensure_language_file_exists(void)
{
    OSI_LOGI(TAG, "[LANG-CHECK] Checking %s existence during boot...", TEMPLATE_LANGUAGE_FILE_PATH);

    int flen = fs_size(TEMPLATE_LANGUAGE_FILE_PATH);
    if (flen > 0) {
        OSI_LOGI(TAG, "[LANG-CHECK] %s already exists (%d bytes). Preserving existing configuration.",
                 TEMPLATE_LANGUAGE_FILE_PATH, flen);
        return 0;
    }

    OSI_LOGI(TAG, "[LANG-CHECK] Fresh boot detected: %s does not exist (flen=%d). Initializing with default audio language...",
             TEMPLATE_LANGUAGE_FILE_PATH, flen);

    soundbox_language_t default_lang = SOUNDBOX_DEFAULT_LANGUAGE;
    const char *num_str = soundbox_language_to_numeric_str(default_lang);
    const char *lang_name = soundbox_language_name(default_lang);

    int fd = fs_open(TEMPLATE_LANGUAGE_FILE_PATH, "wb+");
    if (fd <= 0) {
        OSI_LOGE(TAG, "[LANG-INIT-FAIL] Failed to create %s (fd=%d). Safe fallback to default language (%s: %s).",
                 TEMPLATE_LANGUAGE_FILE_PATH, fd, lang_name, num_str);
        return -1;
    }

    char write_buf[16] = {0};
    snprintf(write_buf, sizeof(write_buf), "%s\n", num_str);
    size_t write_len = strlen(write_buf);

    int written = fs_write((void *)write_buf, 1, write_len, fd);
    fs_close(fd);

    if (written != (int)write_len) {
        OSI_LOGE(TAG, "[LANG-INIT-FAIL] Failed to write default language to %s (written=%d, expected=%u).",
                 TEMPLATE_LANGUAGE_FILE_PATH, written, (unsigned int)write_len);
        return -2;
    }

    OSI_LOGI(TAG, "[LANG-INIT-OK] %s created successfully! Stored default audio language '%s' (code %s).",
             TEMPLATE_LANGUAGE_FILE_PATH, lang_name, num_str);

    /* Also persist to fallback location for redundancy */
    int f_bak = fs_open(TEMPLATE_LANGUAGE_FILE_FALLBACK, "wb+");
    if (f_bak > 0) {
        fs_write((void *)write_buf, 1, write_len, f_bak);
        fs_close(f_bak);
    }

    return 0;
}

int soundbox_load_language(soundbox_language_t *language)
{
    soundbox_language_t detected_lang = SOUNDBOX_DEFAULT_LANGUAGE;
    bool success = false;
    const char *path = NULL;

    int flen = fs_size(TEMPLATE_LANGUAGE_FILE_PATH);
    if (flen <= 0) {
        /* File does not exist yet; ensure created with default language */
        soundbox_ensure_language_file_exists();
        flen = fs_size(TEMPLATE_LANGUAGE_FILE_PATH);
    }

    if (flen > 0) {
        path = TEMPLATE_LANGUAGE_FILE_PATH;
    } else {
        flen = fs_size(TEMPLATE_LANGUAGE_FILE_FALLBACK);
        if (flen > 0) {
            path = TEMPLATE_LANGUAGE_FILE_FALLBACK;
        }
    }

    if (path == NULL) {
        OSI_LOGW(TAG, "[LANG-LOAD] %s not present or empty (flen=%d). Safe fallback: %s (%s)",
                 TEMPLATE_LANGUAGE_FILE_PATH, flen,
                 soundbox_language_name(SOUNDBOX_DEFAULT_LANGUAGE),
                 soundbox_language_to_numeric_str(SOUNDBOX_DEFAULT_LANGUAGE));
        if (language) {
            *language = SOUNDBOX_DEFAULT_LANGUAGE;
        }
        return -1;
    }

    int fd = fs_open(path, "rb");
    if (fd < 0) {
        OSI_LOGE(TAG, "[LANG-LOAD] Failed to open %s for read (fd=%d). Using default fallback: %s (%s)",
                 path, fd, soundbox_language_name(SOUNDBOX_DEFAULT_LANGUAGE), soundbox_language_to_numeric_str(SOUNDBOX_DEFAULT_LANGUAGE));
        if (language) {
            *language = SOUNDBOX_DEFAULT_LANGUAGE;
        }
        return -2;
    }

    char lang_buf[64] = {0};
    int rlen = fs_read(lang_buf, 1, sizeof(lang_buf) - 1, fd);
    fs_close(fd);

    if (rlen <= 0) {
        OSI_LOGE(TAG, "[LANG-LOAD] Empty or unreadable %s (rlen=%d). Using default fallback: %s (%s)",
                 path, rlen, soundbox_language_name(SOUNDBOX_DEFAULT_LANGUAGE), soundbox_language_to_numeric_str(SOUNDBOX_DEFAULT_LANGUAGE));
        if (language) {
            *language = SOUNDBOX_DEFAULT_LANGUAGE;
        }
        return -3;
    }

    lang_buf[rlen] = '\0';
    char *trimmed = prv_str_trim(lang_buf);

    if (trimmed[0] == '\0') {
        OSI_LOGE(TAG, "[LANG-LOAD] %s contains only whitespace. Using default fallback: %s (%s)",
                 path, soundbox_language_name(SOUNDBOX_DEFAULT_LANGUAGE), soundbox_language_to_numeric_str(SOUNDBOX_DEFAULT_LANGUAGE));
        if (language) {
            *language = SOUNDBOX_DEFAULT_LANGUAGE;
        }
        return -4;
    }

    // Support optional JSON format: {"language": "0"} or {"active_language": "1"}
    if (trimmed[0] == '{') {
        cJSON *root = cJSON_Parse(trimmed);
        if (root) {
            const cJSON *j_lang = cJSON_GetObjectItem(root, "language");
            if (!j_lang) j_lang = cJSON_GetObjectItem(root, "active_language");
            if (!j_lang) j_lang = cJSON_GetObjectItem(root, "lang");
            if (!j_lang) j_lang = cJSON_GetObjectItem(root, "lang_support");

            if (j_lang) {
                if (cJSON_IsNumber(j_lang)) {
                    int num = j_lang->valueint;
                    char num_str[16];
                    snprintf(num_str, sizeof(num_str), "%d", num);
                    success = soundbox_str_to_language(num_str, &detected_lang);
                } else if (cJSON_IsString(j_lang) && j_lang->valuestring) {
                    success = soundbox_str_to_language(j_lang->valuestring, &detected_lang);
                }
            }
            cJSON_Delete(root);
        }
    } else {
        success = soundbox_str_to_language(trimmed, &detected_lang);
    }

    if (!success) {
        OSI_LOGE(TAG, "[LANG-LOAD] Invalid/unsupported language code '%s' in %s. Preserving file, using default fallback: %s (%s)",
                 trimmed, path, soundbox_language_name(SOUNDBOX_DEFAULT_LANGUAGE), soundbox_language_to_numeric_str(SOUNDBOX_DEFAULT_LANGUAGE));
        if (language) {
            *language = SOUNDBOX_DEFAULT_LANGUAGE;
        }
        return -5;
    }

    if (language) {
        *language = detected_lang;
    }

    const char *folder = soundbox_get_audio_folder(detected_lang);
    if (folder) {
        OSI_LOGI(TAG, "[LANG-LOAD] Successfully loaded language: %s (code %d) from %s -> Audio folder: %s",
                 soundbox_language_name(detected_lang), (int)detected_lang, path, folder);
    } else {
        OSI_LOGW(TAG, "[LANG-LOAD] Loaded language: %s (code %d) from %s. NOTE: No audio folder configured in firmware!",
                 soundbox_language_name(detected_lang), (int)detected_lang, path);
    }

    return 0;
}

soundbox_language_t soundbox_get_language(void)
{
    prv_lang_lock();
    if (!s_soundbox_lang_initialized) {
        prv_lang_unlock();
        soundbox_language_init();
        prv_lang_lock();
    }
    soundbox_language_t l = s_active_language;
    prv_lang_unlock();
    return l;
}

const char *soundbox_get_current_audio_folder(void)
{
    soundbox_language_t l = soundbox_get_language();
    const char *folder = soundbox_get_audio_folder(l);
    if (folder != NULL) {
        return folder;
    }
    return ENGLISH_AUDIO_FOLDER; // Safe fallback if unconfigured language
}

int soundbox_set_language(soundbox_language_t lang, bool save_to_file)
{
    // Validate enum
    switch (lang) {
        case LANGUAGE_ENGLISH:
        case LANGUAGE_HINDI:
        case LANGUAGE_MARATHI:
        case LANGUAGE_GUJARATI:
        case LANGUAGE_TAMIL:
            break;
        default:
            OSI_LOGE(TAG, "[LANG-SET] Invalid language enum: %d", (int)lang);
            return -1;
    }

    prv_lang_lock();
    s_soundbox_lang_initialized = true;
    s_active_language = lang;

    const char *folder = soundbox_get_audio_folder(lang);
    if (folder != NULL) {
        audio_resource_set_language_prefix(folder);
        OSI_LOGI(TAG, "[LANG-SET] Active language set to %s (code %d), audio folder: %s",
                 soundbox_language_name(lang), (int)lang, folder);
    } else {
        // Missing folder for 3, 4, 7: preserve enum, do not overwrite prefix to English or Hindi
        OSI_LOGW(TAG, "[LANG-SET] Active language set to %s (code %d), but no audio folder configured in firmware. Audio prefix kept as '%s'",
                 soundbox_language_name(lang), (int)lang, audio_resource_get_language_prefix());
    }

    if (save_to_file) {
        const char *num_str = soundbox_language_to_numeric_str(lang);
        int fd = fs_open(TEMPLATE_LANGUAGE_FILE_PATH, "wb+");
        if (fd > 0) {
            fs_write((void *)num_str, 1, strlen(num_str), fd);
            fs_write((void *)"\n", 1, 1, fd);
            fs_close(fd);
            OSI_LOGI(TAG, "[LANG-SAVE] Saved numeric language code '%s' to %s", num_str, TEMPLATE_LANGUAGE_FILE_PATH);
        } else {
            OSI_LOGE(TAG, "[LANG-SAVE] Failed to open %s for write", TEMPLATE_LANGUAGE_FILE_PATH);
        }

        fd = fs_open(TEMPLATE_LANGUAGE_FILE_FALLBACK, "wb+");
        if (fd > 0) {
            fs_write((void *)num_str, 1, strlen(num_str), fd);
            fs_write((void *)"\n", 1, 1, fd);
            fs_close(fd);
        }
    }

    prv_lang_unlock();
    return (folder != NULL) ? 0 : -2;
}

void soundbox_language_init(void)
{
    prv_lang_lock();
    if (s_soundbox_lang_initialized) {
        prv_lang_unlock();
        return;
    }

    /* 1. Ensure language.txt exists during fresh boot */
    soundbox_ensure_language_file_exists();

    /* 2. Load language preference from file */
    soundbox_language_t loaded_lang = SOUNDBOX_DEFAULT_LANGUAGE;
    int ret = soundbox_load_language(&loaded_lang);
    (void)ret;

    s_active_language = loaded_lang;
    s_soundbox_lang_initialized = true;

    const char *folder = soundbox_get_audio_folder(s_active_language);
    if (folder != NULL) {
        audio_resource_set_language_prefix(folder);
        OSI_LOGI(TAG, "[LANG-INIT] Audio subsystem prefix initialized to '%s' (Language: %s)",
                 folder, soundbox_language_name(s_active_language));
    } else {
        audio_resource_set_language_prefix(ENGLISH_AUDIO_FOLDER);
        OSI_LOGW(TAG, "[LANG-INIT] Language %s (%d) has no configured audio folder. Fallback playback to %s",
                 soundbox_language_name(s_active_language), (int)s_active_language, ENGLISH_AUDIO_FOLDER);
    }

    prv_lang_unlock();
}
