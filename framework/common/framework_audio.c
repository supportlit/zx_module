/**
 * @file audio_resource_manager.c
 * @brief Audio Resource Manager Implementation for ZxInfotek Soundbox
 * @version 1.0
 * @date 2025-11-20
 */

#include "framework_audio.h"
#include "framework_log.h"
#include "audio_resource_map.h"  /* Customer-specific resource map */
#include "framework_config.h"      /* For GPIO definitions */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#ifndef __cplusplus
#ifndef bool
typedef unsigned char bool;
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#endif
#include "osi_api.h"               /* RTOS API */
#include "typedef.h"               /* Basic types */
#include "gpio_api.h"              /* GPIO API */
#include "audio_api.h"
#include "fs_api.h"
#include "drv_def.h"
#include "drv_iomux.h"
#include "fs_api.h"

#define TAG "AUDIO_RESOURCE"

/**
 * @brief Weak stub for mp3IsPlaying (not provided in core_stub)
 */
#if defined(__GNUC__)
__attribute__((weak))
#endif
int mp3IsPlaying(void)
{
    // Default: not playing
    return 0;
}

/**
 * @brief Audio resource map entry (matches generated header)
 * @note This typedef should match the structure in audio_resource_map.h
 */
typedef struct {
    audio_resource_id_t id;
    const char *filename;  /* Single filename, language selected via folder prefix */
} audio_resource_map_t;

/**
 * @brief Language path prefixes
 */
#define AUDIO_LANG_PREFIX_FILE    "D:/audio_lang_prefix.bin"
#define AUDIO_LANG_PREFIX_MAGIC   0x4c504658u /* 'LPFX' */
#if __has_include("soundbox_language.h")
#include "soundbox_language.h"
#endif

#if defined(SOUNDBOX_DEFAULT_AUDIO_FOLDER)
#define AUDIO_LANG_PREFIX_DEFAULT SOUNDBOX_DEFAULT_AUDIO_FOLDER
#else
#define AUDIO_LANG_PREFIX_DEFAULT "D:/ENG/"
#endif

typedef struct {
    uint32_t magic;
    char prefix[64];
} audio_lang_prefix_store_t;

// Support dynamic language prefix (e.g., set by application interface)
static char s_dynamic_language_prefix[64] = AUDIO_LANG_PREFIX_DEFAULT;
static bool s_audio_prefix_loaded = false;
static bool s_audio_stop_enable = false;
static bool s_audio_is_playing = false;
static uint32_t s_audio_valid_time = 3000;

static void prv_audio_load_language_prefix(void);
static void prv_audio_save_language_prefix(void);


// Provide interface to set language prefix
void audio_resource_set_language_prefix(const char *prefix)
{
    if (!prefix) {
        OSI_LOGE(TAG, "audio_resource: language prefix null");
        return;
    }

    size_t len = strlen(prefix);
    if (len == 0 || len >= sizeof(s_dynamic_language_prefix)) {
        OSI_LOGE(TAG, "audio_resource: language prefix invalid len=%u", (unsigned)len);
        return;
    }

    if (strcmp(s_dynamic_language_prefix, prefix) == 0) {
        return; /* No change */
    }

    strcpy(s_dynamic_language_prefix, prefix);
    s_audio_prefix_loaded = true;
    prv_audio_save_language_prefix();
}

char* audio_resource_get_language_prefix(void)
{
    if(s_dynamic_language_prefix != NULL){
        return s_dynamic_language_prefix;
    }else{
        return NULL;
    }
}

static void prv_audio_save_language_prefix(void)
{
    audio_lang_prefix_store_t store = {0};
    store.magic = AUDIO_LANG_PREFIX_MAGIC;
    strncpy(store.prefix, s_dynamic_language_prefix, sizeof(store.prefix) - 1);

    int fd = fs_open(AUDIO_LANG_PREFIX_FILE, "wb+");
    if (fd < 0) {
        OSI_LOGE(TAG, "audio_resource: failed to open %s for write", AUDIO_LANG_PREFIX_FILE);
        return;
    }

    ssize_t written = fs_write(&store, 1, sizeof(store), fd);
    fs_close(fd);

    if (written != sizeof(store)) {
        OSI_LOGE(TAG, "audio_resource: failed to save language prefix (ret=%d)", (int)written);
    } else {
        OSI_LOGI(TAG, "audio_resource: language prefix saved (%s)", store.prefix);
    }
}

static void prv_audio_load_language_prefix(void)
{
    if (s_audio_prefix_loaded) {
        return;
    }

    s_audio_prefix_loaded = true;

    int fd = fs_open(AUDIO_LANG_PREFIX_FILE, "rb");
    if (fd < 0) {
        return; /* Use default */
    }

    audio_lang_prefix_store_t store = {0};
    ssize_t read_items = fs_read(&store, 1, sizeof(store), fd);
    fs_close(fd);

    if (read_items != sizeof(store) || store.magic != AUDIO_LANG_PREFIX_MAGIC) {
        OSI_LOGW(TAG, "audio_resource: invalid language prefix file, using default");
        return;
    }

    store.prefix[sizeof(store.prefix) - 1] = '\0';
    if (store.prefix[0] == '\0') {
        OSI_LOGW(TAG, "audio_resource: persisted language prefix empty, using default");
        return;
    }

    strncpy(s_dynamic_language_prefix, store.prefix, sizeof(s_dynamic_language_prefix) - 1);
    s_dynamic_language_prefix[sizeof(s_dynamic_language_prefix) - 1] = '\0';
    OSI_LOGI(TAG, "audio_resource: language prefix restored (%s)", s_dynamic_language_prefix);
}

/* Audio resource map is now customer-specific, included from audio_resource_map.h */
#define AUDIO_QUEUE_URGENT_LEN  2
#define AUDIO_QUEUE_HIGH_LEN  10
#define AUDIO_QUEUE_MID_LEN   20
#define AUDIO_QUEUE_LOW_LEN   30
#define AUDIO_THREAD_PRIORITY AUDIO_TASK_PRIORITY
#define AUDIO_THREAD_STACK    (20 * 1024)
#define MERGE_MP3_BUF_SIZE    (0x28000)
static uint8_t *g_merge_mp3_buf = NULL;
#define FILE_PATH_LEN_MAX     256
#define ID3V2_HEADER_SIZE 10
#define ID3V1_TAG_SIZE 128

#define AUDIO_NOTIFY_ERROR(_cb, _res_id, _err)                 \
    do {                                                        \
        if ((_cb) != NULL) {                                    \
            (_cb)((_res_id), AUDIO_PLAYBACK_STATUS_ERROR, (_err)); \
        }                                                       \
    } while (0)

#define AUDIO_RETURN_ERROR(_cb, _res_id, _err) \
    do {                                        \
        int __audio_err = (_err);               \
        AUDIO_NOTIFY_ERROR((_cb), (_res_id), __audio_err); \
        return __audio_err;                     \
    } while (0)

static int prv_build_audio_path(audio_resource_id_t res_id, char *path_buffer, uint32_t buffer_size);

/**
 * @brief Audio message types for queue
 */
typedef enum {
    AUDIO_MSG_TYPE_FILE_MP3,
    AUDIO_MSG_TYPE_SEQUENCE_MP3,
    AUDIO_MSG_TYPE_STREAM_MP3,
    AUDIO_MSG_TYPE_SEQUENCE_STREAM_MP3,
    AUDIO_MSG_TYPE_STOP,
    AUDIO_MSG_TYPE_MAX
} audio_msg_type_t;

typedef enum {
    AUDIO_COMBO_SOURCE_RES_ID = 0,
    AUDIO_COMBO_SOURCE_PATH = 1,
} audio_combo_source_t;

typedef struct {
    audio_combo_source_t source;
    uint32_t count;
    audio_resource_id_t ids[AUDIO_COMBO_MAX_FILES];
    char **paths;
} audio_combo_request_t;

/**
 * @brief Audio message structure
 * @note file_path is dynamically allocated to reduce queue memory usage
 */
typedef struct {
    audio_msg_type_t type;
    audio_resource_id_t res_id;
    char *file_path;  /* Dynamically allocated */
    audio_playback_callback_t callback;
    audio_combo_request_t *combo_req;
    char *stream_buf;
    uint32_t stream_size;
    audio_sequence_stream_builder_t seq_builder;
    void *seq_builder_ctx;
    uint32_t tick;
    uint8_t stop_flag;
} audio_msg_t;

static void prv_free_combo_request(audio_combo_request_t *combo_req)
{
    if (!combo_req) {
        return;
    }

    if (combo_req->paths) {
        for (uint32_t i = 0; i < combo_req->count; ++i) {
            if (combo_req->paths[i]) {
                free(combo_req->paths[i]);
            }
        }
        free(combo_req->paths);
        combo_req->paths = NULL;
    }

    free(combo_req);
}

static void prv_free_audio_msg(audio_msg_t *msg)
{
    if (!msg) {
        return;
    }

    if (msg->file_path) {
        free(msg->file_path);
        msg->file_path = NULL;
    }

    if (msg->combo_req) {
        prv_free_combo_request(msg->combo_req);
        msg->combo_req = NULL;
    }

    if(msg->stream_buf){
        free(msg->stream_buf);
        msg->stream_buf = NULL;
    }
}

static char *prv_trim_token(char *token)
{
    if (!token) {
        return NULL;
    }

    while (*token && isspace((unsigned char)*token)) {
        token++;
    }

    char *end = token + strlen(token);
    while (end > token && isspace((unsigned char)*(end - 1))) {
        *(--end) = '\0';
    }

    return token;
}

/**
 * @brief Manager state structure
 */
typedef struct {
    audio_resource_config_t config;
    bool initialized;
    bool stop;
    audio_playback_callback_t current_callback;
    audio_resource_id_t current_res_id;
    /* Thread and queue management */
    osiThread_t *audio_thread;
    osiMessageQueue_t *audio_queue_urgent;  /* Urgent priority queue */
    osiMessageQueue_t *audio_queue_high;  /* High priority queue */
    osiMessageQueue_t *audio_queue_mid;   /* Medium priority queue */
    osiMessageQueue_t *audio_queue_low;   /* Low priority queue */
    osiSemaphore_t *play_semaphore;       /* Playback control */
    osiSemaphore_t *complete_semaphore;   /* Playback completion */
    /* Hardware control */
    bool pa_enabled;
    bool codec_enabled;
    /* Stream combo playback */
    uint8_t *combo_stream_buffer;
    uint32_t combo_stream_size;
    bool combo_stream_active;
} audio_resource_manager_t;

static audio_resource_manager_t s_manager = {
    .initialized = false,
    .current_callback = NULL,
    .current_res_id = AUDIO_RES_MAX,
    .audio_thread = NULL,
    .audio_queue_urgent = NULL,
    .audio_queue_high = NULL,
    .audio_queue_mid = NULL,
    .audio_queue_low = NULL,
    .play_semaphore = NULL,
    .complete_semaphore = NULL,
    .pa_enabled = false,
    .codec_enabled = false,
    .combo_stream_buffer = NULL,
    .combo_stream_size = 0,
    .combo_stream_active = false,
};
static uint8_t volume = 1;

static int prv_queue_combo_request(audio_combo_request_t *combo_req,
                                   audio_playback_callback_t callback,
                                   audio_resource_id_t res_hint)
{
    if (!combo_req || combo_req->count == 0) {
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, res_hint, -1);
    }

    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_SEQUENCE_MP3;
    msg.res_id = res_hint;
    msg.combo_req = combo_req;
    msg.callback = callback;

    if (!osiMessageQueueTryPut(s_manager.audio_queue_high, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: combo queue full");
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, res_hint, -2);
    }

    OSI_LOGI(TAG, "audio_resource: combo async queued count=%lu", combo_req->count);
    return 0;
}


static int merge_mp3_files(char **file, char *buf, int *out_size)
{
    int ret = 0;
    int i = 0;
    int len = 0;
    int fd = 0;
    int tmp_size = 0;

    if ((file == NULL) || (buf == NULL) || (out_size == NULL)) {
        return -1;
    }

    *out_size = 0;

    for (i = 0; file[i] != NULL; i++) {
        const char *path = file[i];
        if (path == NULL) {
            continue;
        }

        len = fs_size(path);
        if (len <= 0) {
            OSI_LOGE(TAG, "merge_mp3_files: invalid file %s", path);
            ret = -1;
            break;
        }

        fd = fs_open(path, "rb");
        if (fd <= 0) {
            OSI_LOGE(TAG, "merge_mp3_files: open %s failed", path);
            ret = -1;
            break;
        }

        uint8_t *scratch = (uint8_t *)malloc(len > 0 ? len : 1);
        if (!scratch) {
            OSI_LOGE(TAG, "merge_mp3_files: malloc scratch failed for %s", path);
            fs_close(fd);
            ret = -3;
            break;
        }

        /* ID3V2 | Data | ID3V1 */
        if (len >= ID3V1_TAG_SIZE) {
            fs_seek(fd, -ID3V1_TAG_SIZE, SEEK_END);
            fs_read(scratch, 1, ID3V1_TAG_SIZE, fd);
            if (memcmp(scratch, "TAG", 3) == 0) {
                len -= ID3V1_TAG_SIZE;
                OSI_LOGD(TAG, "merge_mp3_files: id3v1 trimmed, %s len:%d", path, len);
            }
            fs_seek(fd, 0, SEEK_SET);
        }

        if (len >= ID3V2_HEADER_SIZE) {
            fs_read(scratch, 1, ID3V2_HEADER_SIZE, fd);
            if (memcmp(scratch, "ID3", 3) == 0) {
                tmp_size = ((scratch[6] & 0x7F) << 21) |
                           ((scratch[7] & 0x7F) << 14) |
                           ((scratch[8] & 0x7F) << 7) |
                           (scratch[9] & 0x7F);
                fs_seek(fd, tmp_size, SEEK_CUR);
                len -= (ID3V2_HEADER_SIZE + tmp_size);
                OSI_LOGD(TAG, "merge_mp3_files: id3v2 trimmed %s len:%d", path, len);
            } else {
                fs_seek(fd, 0, SEEK_SET);
            }
        }

        while (len > 0) {
            tmp_size = (len > MERGE_MP3_BUF_SIZE) ? MERGE_MP3_BUF_SIZE : len;
            len -= tmp_size;

            if ((*out_size + tmp_size) > MERGE_MP3_BUF_SIZE) {
                OSI_LOGE(TAG, "merge_mp3_files: buffer overflow");
                ret = -2;
                break;
            }

            fs_read(scratch, 1, tmp_size, fd);
            memcpy(buf + *out_size, scratch, tmp_size);
            *out_size += tmp_size;
        }

        free(scratch);
        fs_close(fd);
        fd = 0;

        if (ret != 0) {
            break;
        }
    }

    if (fd > 0) {
        fs_close(fd);
    }

    return ret;
}

static void prv_release_stream_buffer(void)
{
    s_manager.combo_stream_buffer = NULL;
    s_manager.combo_stream_size = 0;
    s_manager.combo_stream_active = false;
}

/**
 * @brief Find resource filename by ID
 * @note Searches customer-provided resource map, returns NULL if not found
 */
static const char *prv_find_resource_filename(audio_resource_id_t res_id)
{
    /* Search in customer audio map */
    for (uint32_t i = 0; i < CUSTOMER_AUDIO_MAP_COUNT; i++) {
        if (CUSTOMER_AUDIO_MAP[i].id == res_id) {
            /* Return filepath (can be NULL if customer didn't provide) */
            return CUSTOMER_AUDIO_MAP[i].filepath;
        }
    }
    return NULL;
}

/**
 * @brief Enable/Disable PA (Power Amplifier) and Codec
 */
static void prv_audio_pa_codec_enable(bool enable)
{
#ifdef CUSTOMER_SOUNDBOX_SD_MODULE
    if (enable && volume <= 1) {
        s_manager.pa_enabled = false;
        s_manager.codec_enabled = false;
        OSI_LOGD(TAG, "audio_resource: PA/Codec enable skipped due to mute (vol=%d)", volume);
        return;
    }
#endif

    GPIOConfiguration cfg = {
        .pinDir = GPIO_OUT_PIN,
        .initLv = enable ? 1 : 0,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_NO_EDGE,
        .isr = NULL,
        .wu = NULL,
    };
    
    /* Configure PA GPIO */
    GpioInitConfiguration(GPIO_PA, cfg);
    
    /* Configure Codec GPIO */
    #if defined(FRAMEWORK_LCD_POWER_ENABLE) || defined(CUSTOMER_SOUNDBOX_SD_MODULE)
        // The TH power supply is always on
    #else
    GpioInitConfiguration(GPIO_CODEC, cfg);
    #endif
    
    s_manager.pa_enabled = enable;
    s_manager.codec_enabled = enable;
    
    OSI_LOGD(TAG, "audio_resource: PA/Codec %s", enable ? "enabled" : "disabled");
}

/**
 * @brief MP3 playback event callback
 */
static void prv_mp3_playback_callback(Mp3PlayEventType event, int status)
{
    if (event == MP3_PLAYBACK_EVENT_STATUS && status == MP3_PLAYBACK_STATUS_ENDED) {  /* MP3_PLAYBACK_EVENT_STATUS && MP3_PLAYBACK_STATUS_ENDED */
        /* Playback completed */
        OSI_LOGI(TAG, "audio_resource: Playback completed");
        
        if (s_manager.complete_semaphore) {
            osiSemaphoreRelease(s_manager.complete_semaphore);
        }
        
        prv_release_stream_buffer();

        /* Disable PA/Codec after playback */
        prv_audio_pa_codec_enable(false);
        
        /* Invoke user callback */
        if (s_manager.current_callback) {
            s_manager.current_callback(s_manager.current_res_id, AUDIO_PLAYBACK_STATUS_ENDED, 0);
            s_manager.current_callback = NULL;
        }
        
        s_manager.current_res_id = AUDIO_RES_MAX;
    } else if (event == MP3_PLAYBACK_EVENT_STATUS && status == MP3_PLAYBACK_STATUS_STARTED) {  /* MP3_PLAYBACK_EVENT_STATUS && MP3_PLAYBACK_STATUS_STARTED */
        /* Playback started */
        OSI_LOGI(TAG, "audio_resource: Playback started");
        
        if (s_manager.current_callback) {
            s_manager.current_callback(s_manager.current_res_id, AUDIO_PLAYBACK_STATUS_STARTED, 0);
        }
    }
}

/**
 * @brief Play audio file with PA/Codec control
 */
static int prv_play_audio_file(const char *file_path, audio_resource_id_t res_id, audio_playback_callback_t callback)
{
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Invalid file path");
        AUDIO_RETURN_ERROR(callback, res_id, -1);
    }
    
    /* Check file exists */
    int file_size = fs_size(file_path);
    if (file_size <= 0) {
        OSI_LOGE(TAG, "audio_resource: File not found or empty: %s", file_path);
        AUDIO_RETURN_ERROR(callback, res_id, -2);
    }
    
    /* Enable PA and Codec */
    prv_audio_pa_codec_enable(true);
    
    /* Store callback and resource ID */
    s_manager.current_callback = callback;
    s_manager.current_res_id = res_id;
    
    Mp3PlayConfigInfo config = {
        .block_size = 20 * 1024,
        .trigger = prv_mp3_playback_callback
    };
    
    /* Start playback */
    OSI_LOGI(TAG, "audio_resource: Playing file: %s (size=%d)", file_path, file_size);
    int ret = mp3PlayStart(file_path, &config, NULL);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to start playback, error=%d", ret);
        prv_audio_pa_codec_enable(false);
        AUDIO_RETURN_ERROR(callback, res_id, ret);
    }
    
    /* Wait for playback completion (bounded 15s timeout prevents audio queue deadlock on corrupt stream) */
    if (s_manager.complete_semaphore) {
        osiSemaphoreTryAcquire(s_manager.complete_semaphore, 15 * 1000);
    }
    
    /* Add delay based on file size */
    if (file_size < 6 * 1024) {
        osiThreadSleepMs(100);
    } else {
        osiThreadSleepMs(70);
    }
    
    return 0;
}

static int prv_play_combo_stream(audio_combo_request_t *combo_req, audio_playback_callback_t callback)
{
    if (!combo_req || combo_req->count == 0) {
        OSI_LOGE(TAG, "audio_resource: Invalid combo request");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }

    if (combo_req->count > AUDIO_COMBO_MAX_FILES) {
        OSI_LOGW(TAG, "audio_resource: combo count limited to %u", AUDIO_COMBO_MAX_FILES);
        combo_req->count = AUDIO_COMBO_MAX_FILES;
    }

    char path_buffer[AUDIO_COMBO_MAX_FILES][FILE_PATH_LEN_MAX] = {0};
    char *path_list[AUDIO_COMBO_MAX_FILES + 1] = {0};
    audio_resource_id_t first_res_id = AUDIO_RES_MAX;

    if (combo_req->source == AUDIO_COMBO_SOURCE_RES_ID) {
        for (uint32_t i = 0; i < combo_req->count; ++i) {
            int ret = prv_build_audio_path(combo_req->ids[i], path_buffer[i], sizeof(path_buffer[i]));
            if (ret != 0) {
                OSI_LOGE(TAG, "audio_resource: combo path build failed idx=%ld", i);
                AUDIO_RETURN_ERROR(callback, combo_req->ids[i], ret);
            }
            path_list[i] = path_buffer[i];
        }
        first_res_id = combo_req->ids[0];
    } else {
        for (uint32_t i = 0; i < combo_req->count; ++i) {
            if (!combo_req->paths || !combo_req->paths[i]) {
                OSI_LOGE(TAG, "audio_resource: combo path entry missing idx=%lu", i);
                AUDIO_RETURN_ERROR(callback, first_res_id, -2);
            }
            path_list[i] = combo_req->paths[i];
        }
    }
    path_list[combo_req->count] = NULL;

    prv_release_stream_buffer();

    s_manager.combo_stream_buffer = g_merge_mp3_buf;

    int merged_size = 0;
    int merge_ret = merge_mp3_files(path_list, (char *)s_manager.combo_stream_buffer, &merged_size);
    if (merge_ret != 0 || merged_size <= 0) {
        int err = (merge_ret != 0) ? merge_ret : -4;
        OSI_LOGE(TAG, "audio_resource: combo merge failed ret=%d size=%d", merge_ret, merged_size);
        prv_release_stream_buffer();
        AUDIO_RETURN_ERROR(callback, first_res_id, err);
    }

    s_manager.combo_stream_size = (uint32_t)merged_size;
    s_manager.combo_stream_active = true;

    prv_audio_pa_codec_enable(true);

    s_manager.current_callback = callback;
    s_manager.current_res_id = first_res_id;

    int ret = mp3StreamPlay(s_manager.combo_stream_buffer, merged_size, prv_mp3_playback_callback, NULL);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: combo stream play failed ret=%d", ret);
        prv_audio_pa_codec_enable(false);
        prv_release_stream_buffer();
        AUDIO_RETURN_ERROR(callback, first_res_id, ret);
    }

    if (s_manager.complete_semaphore) {
        osiSemaphoreTryAcquire(s_manager.complete_semaphore, 15 * 1000); /* 15s timeout prevents worker deadlock */
    }
    mp3PlayStop(0xffffffff, 1);
    prv_release_stream_buffer();
    return 0;
}

static int prv_play_raw_stream(char *buf, uint32_t buf_size, audio_playback_callback_t callback)
{
    if (buf == NULL || buf_size == 0) {
        OSI_LOGE(TAG, "audio_resource: invalid stream buffer");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }

    if(MERGE_MP3_BUF_SIZE < buf_size)
    {
        OSI_LOGE(TAG, "audio_resource: buffer size insufficient");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }
    prv_release_stream_buffer();

    s_manager.combo_stream_buffer = g_merge_mp3_buf;
    memcpy(g_merge_mp3_buf, buf, buf_size);
    s_manager.combo_stream_size = buf_size;
    s_manager.combo_stream_active = true;

    prv_audio_pa_codec_enable(true);

    s_manager.current_callback = callback;
    s_manager.current_res_id = 0;

    int ret = mp3StreamPlay(s_manager.combo_stream_buffer, s_manager.combo_stream_size, prv_mp3_playback_callback, NULL);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: combo stream play failed ret=%d", ret);
        prv_audio_pa_codec_enable(false);
        prv_release_stream_buffer();
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, ret);
    }

    if (s_manager.complete_semaphore) {
        osiSemaphoreTryAcquire(s_manager.complete_semaphore, 15 * 1000); /* 15s timeout prevents worker deadlock */
    }

    prv_release_stream_buffer();
    osiThreadSleepMs(100);

    return 0;
}

static int prv_play_sequence_stream(audio_sequence_stream_builder_t builder, void *builder_ctx,
                                    audio_playback_callback_t callback)
{
    if (!builder) {
        OSI_LOGE(TAG, "audio_resource: sequence builder missing");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }

    prv_release_stream_buffer();

    s_manager.combo_stream_buffer = g_merge_mp3_buf;
    s_manager.combo_stream_size = 0;
    uint32_t built_size = 0;

    int build_ret = builder(builder_ctx, (char *)s_manager.combo_stream_buffer, MERGE_MP3_BUF_SIZE, &built_size);
    if (build_ret != 0) {
        OSI_LOGE(TAG, "audio_resource: sequence builder failed ret=%d", build_ret);
        prv_release_stream_buffer();
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, build_ret);
    }

    if (built_size == 0 || built_size > MERGE_MP3_BUF_SIZE) {
        OSI_LOGE(TAG, "audio_resource: sequence builder size invalid=%u", (unsigned)built_size);
        prv_release_stream_buffer();
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }

    s_manager.combo_stream_size = built_size;
    s_manager.combo_stream_active = true;

    prv_audio_pa_codec_enable(true);

    s_manager.current_callback = callback;
    s_manager.current_res_id = AUDIO_RES_MAX;

    int ret = mp3StreamPlay(s_manager.combo_stream_buffer, s_manager.combo_stream_size, prv_mp3_playback_callback, NULL);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: sequence stream play failed ret=%d", ret);
        prv_audio_pa_codec_enable(false);
        prv_release_stream_buffer();
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, ret);
    }

    if (s_manager.complete_semaphore) {
        osiSemaphoreTryAcquire(s_manager.complete_semaphore, 15 * 1000); /* 15s timeout prevents worker deadlock */
    }

    prv_release_stream_buffer();
    osiThreadSleepMs(100);

    return 0;
}

/**
 * @brief Process audio message from queue
 */
static bool prv_process_audio_msg(audio_msg_t *msg)
{
    if (!msg) {
        return false;
    }
    
    bool result = true;
    s_audio_is_playing = true;
    switch (msg->type) {
    case AUDIO_MSG_TYPE_FILE_MP3:
        if (msg->file_path) {
            prv_play_audio_file(msg->file_path, msg->res_id, msg->callback);
        }
        break;
        
    case AUDIO_MSG_TYPE_SEQUENCE_MP3:
        if (msg->combo_req) {
            prv_play_combo_stream(msg->combo_req, msg->callback);
        }
        break;
    
    case AUDIO_MSG_TYPE_STREAM_MP3:
        // Currently not implemented
        prv_play_raw_stream(msg->stream_buf, msg->stream_size, msg->callback);
        break;
    case AUDIO_MSG_TYPE_SEQUENCE_STREAM_MP3:
        prv_play_sequence_stream(msg->seq_builder, msg->seq_builder_ctx, msg->callback);
        break;
    case AUDIO_MSG_TYPE_STOP:
        mp3PlayStop(0xffffffff, 1);
        prv_audio_pa_codec_enable(false);
        break;
        
    default:
        OSI_LOGW(TAG, "audio_resource: Unknown message type: %u", msg->type);
        result = false;
        break;
    }
    s_audio_is_playing = false;
    prv_free_audio_msg(msg);
    return result;
}

/**
 * @brief Audio playback thread entry
 */
static void prv_audio_thread_entry(void *param)
{
    audio_msg_t msg;
    
    OSI_LOGI(TAG, "audio_resource: Audio thread started");
    
    while (1) {
        while (s_manager.stop)
        {
            osiThreadSleep(1000);
        }
        

        memset(&msg, 0, sizeof(msg));
        /* Process high priority queue first */
        if (osiMessageQueueTryGet(s_manager.audio_queue_urgent, &msg, 10)) {
            osiSemaphoreAcquire(s_manager.play_semaphore);
            prv_process_audio_msg(&msg);
            osiSemaphoreRelease(s_manager.play_semaphore);
            osiThreadSleepMs(100);
            continue;
        }
        
        /* Process high priority queue first */
        if (osiMessageQueueTryGet(s_manager.audio_queue_high, &msg, 10)) {
            osiSemaphoreAcquire(s_manager.play_semaphore);
            prv_process_audio_msg(&msg);
            osiSemaphoreRelease(s_manager.play_semaphore);
            osiThreadSleepMs(100);
            continue;
        }
        
        /* Process medium priority queue */
        if (osiMessageQueueTryGet(s_manager.audio_queue_mid, &msg, 2)) {
            if(msg.tick && osiGetTicks() - msg.tick > (s_audio_valid_time/5))
            {
                OSI_LOGI(TAG, "audio_resource: skip mid priority msg due to tick");
                prv_free_audio_msg(&msg);
                continue;
            }
            osiSemaphoreAcquire(s_manager.play_semaphore);
            if(msg.stop_flag){
                s_audio_stop_enable = true;
            }
            prv_process_audio_msg(&msg);
            if(s_audio_stop_enable){
                s_audio_stop_enable = false;
            }
            osiSemaphoreRelease(s_manager.play_semaphore);
            osiThreadSleepMs(100);
            continue;
        }
        
        /* Process low priority queue */
        if (osiMessageQueueTryGet(s_manager.audio_queue_low, &msg, 2)) {
            osiSemaphoreAcquire(s_manager.play_semaphore);
            prv_process_audio_msg(&msg);
            osiSemaphoreRelease(s_manager.play_semaphore);
            osiThreadSleepMs(100);
            continue;
        }
        
        /* No messages, sleep */
        osiThreadSleepMs(200);
    }
}

/**
 * @brief Build complete audio file path
 * @note Combines language prefix (D:/ENG/) with filename from resource map
 */
static int prv_build_audio_path(audio_resource_id_t res_id, char *path_buffer, uint32_t buffer_size)
{
    if (!path_buffer || buffer_size == 0) {
        OSI_LOGE(TAG, "audio_resource: Invalid path buffer");
        return -1;
    }
    
    const char *filename = prv_find_resource_filename(res_id);
    
    /* Check if customer provided this resource */
    if (!filename) {
        OSI_LOGW(TAG, "audio_resource: Resource ID %d not provided by customer, skipping", res_id);
        return -2;  /* Not an error, just not provided */
    }
    
    /* Only use the dynamically set prefix */
    if (s_dynamic_language_prefix[0] == '\0') {
        OSI_LOGE(TAG, "audio_resource: No language prefix set");
        return -3;
    }
    int len = snprintf(path_buffer, buffer_size, "%s%s", s_dynamic_language_prefix, filename);
    
    if (len < 0 || (uint32_t)len >= buffer_size) {
        OSI_LOGE(TAG, "audio_resource: Path buffer too small");
        return -3;
    }
    
    OSI_LOGD(TAG, "audio_resource: Built path: %s", path_buffer);
    return 0;
}

static void prv_gpio_init(void)
{
    unsigned int mfp_spk[] = {
        MFP_REG(PA_PINMUX)   | PA_PINFUN | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(CODEC_PINMUX) | CODEC_PINFUN | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    drvGpioPinMuxConfig(mfp_spk);
}

/**
 * @brief Initialize audio resource manager
 */
int audio_resource_manager_init(const audio_resource_config_t *config)
{
    if (s_manager.initialized) {
        OSI_LOGW(TAG, "audio_resource: Already initialized");
        return 0;
    }
    prv_audio_load_language_prefix();

    if(g_merge_mp3_buf == NULL)
    {
        g_merge_mp3_buf = (uint8_t *)malloc(MERGE_MP3_BUF_SIZE);
        if (g_merge_mp3_buf == NULL)
        {
            OSI_LOGE(TAG, "audio_resource: Failed to allocate merge mp3 buffer");
            return -7;
        }
    }
    if (config) {
        memcpy(&s_manager.config, config, sizeof(audio_resource_config_t));
    } else {
        s_manager.config.max_path_length = 256;
        s_manager.config.auto_fallback = true;
    }
    prv_gpio_init();
    /* Initialize PA and Codec GPIO */
    GPIOConfiguration gpio_cfg = {
        .pinDir = GPIO_OUT_PIN,
        .initLv = 0,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_NO_EDGE,
        .isr = NULL,
        .wu = NULL,
    };
    GpioInitConfiguration(GPIO_PA, gpio_cfg);
    GpioInitConfiguration(GPIO_CODEC, gpio_cfg);
    /* Create message queues */
    s_manager.audio_queue_urgent = osiMessageQueueCreate(AUDIO_QUEUE_URGENT_LEN, sizeof(audio_msg_t));
    if (!s_manager.audio_queue_urgent) {
        OSI_LOGE(TAG, "audio_resource: Failed to create urgent priority queue");
        return -1;
    }

    s_manager.audio_queue_high = osiMessageQueueCreate(AUDIO_QUEUE_HIGH_LEN, sizeof(audio_msg_t));
    if (!s_manager.audio_queue_high) {
        OSI_LOGE(TAG, "audio_resource: Failed to create high priority queue");
        return -1;
    }
    s_manager.audio_queue_mid = osiMessageQueueCreate(AUDIO_QUEUE_MID_LEN, sizeof(audio_msg_t));
    if (!s_manager.audio_queue_mid) {
        OSI_LOGE(TAG, "audio_resource: Failed to create medium priority queue");
        return -2;
    }
    s_manager.audio_queue_low = osiMessageQueueCreate(AUDIO_QUEUE_LOW_LEN, sizeof(audio_msg_t));
    if (!s_manager.audio_queue_low) {
        OSI_LOGE(TAG, "audio_resource: Failed to create low priority queue");
        return -3;
    }
    /* Create semaphores */
    s_manager.play_semaphore = osiSemaphoreCreate(1, 1);
    if (!s_manager.play_semaphore) {
        OSI_LOGE(TAG, "audio_resource: Failed to create play semaphore");
        return -4;
    }
    s_manager.complete_semaphore = osiSemaphoreCreate(1, 0);
    if (!s_manager.complete_semaphore) {
        OSI_LOGE(TAG, "audio_resource: Failed to create complete semaphore");
        return -5;
    }
    /* Create audio playback thread */
    s_manager.audio_thread = osiThreadCreate("AUDIO_RES", prv_audio_thread_entry, NULL, 
                                             AUDIO_THREAD_PRIORITY, AUDIO_THREAD_STACK);
    if (!s_manager.audio_thread) {
        OSI_LOGE(TAG, "audio_resource: Failed to create audio thread");
        return -6;
    }
    s_manager.initialized = true;
    OSI_LOGI(TAG, "audio_resource: Initialized, thread created");
    return 0;
}

/**
 * @brief Deinitialize audio resource manager
 */
int audio_resource_manager_deinit(void)
{
    if (!s_manager.initialized) {
        return 0;
    }
    
    /* Stop any playing audio */
    audio_resource_stop();
    
    /* Disable PA and Codec */
    prv_audio_pa_codec_enable(false);
    
    /* Note: Thread and queue cleanup should be done here if needed */
    /* For now, we just mark as uninitialized */
    if(g_merge_mp3_buf != NULL)
    {
        free(g_merge_mp3_buf);
        g_merge_mp3_buf = NULL;
    }
    
    s_manager.initialized = false;
    
    OSI_LOGI(TAG, "audio_resource: Deinitialized");
    return 0;
}



/**
 * @brief Get audio file path for a resource ID
 */
int audio_resource_get_path(audio_resource_id_t res_id, char *path_buffer, uint32_t buffer_size)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        return -1;
    }
    
    return prv_build_audio_path(res_id, path_buffer, buffer_size);
}

/**
 * @brief Play an audio resource (using medium priority queue)
 */
int audio_resource_play(audio_resource_id_t res_id, audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, res_id, -1);
    }
    
    if (res_id >= AUDIO_RES_MAX) {
        OSI_LOGE(TAG, "audio_resource: Invalid resource ID %d", res_id);
        AUDIO_RETURN_ERROR(callback, res_id, -2);
    }
    
    /* Allocate memory for file path */
    char *file_path = (char *)malloc(256);
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Failed to allocate memory for path");
        AUDIO_RETURN_ERROR(callback, res_id, -3);
    }
    
    /* Build audio file path */
    int ret = prv_build_audio_path(res_id, file_path, 256);
    if (ret == -2) {
        /* Resource not provided by customer, skip playback silently */
        OSI_LOGD(TAG, "audio_resource: Resource %d not provided, skipping playback", res_id);
        free(file_path);
        if (callback) {
            callback(res_id, AUDIO_PLAYBACK_STATUS_SKIPPED, 0);
        }
        return 0;  /* Not an error */
    } else if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to build path for res_id=%d", res_id);
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, ret);
    }
    
    /* Prepare message */
    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_FILE_MP3;
    msg.res_id = res_id;
    msg.file_path = file_path;
    msg.callback = callback;
    
    /* Push to medium priority queue */
    if (!osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: Failed to queue audio message");
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, -4);
    }
    
    OSI_LOGI(TAG, "audio_resource: Queued res_id=%d, path=%s", res_id, file_path);
    return 0;
}

/**
 * @brief Play an audio file by absolute path (no language prefix applied)
 */
int audio_file_play(const char *file_path, audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }

    if (!file_path || file_path[0] == '\0') {
        OSI_LOGE(TAG, "audio_resource: Invalid file path");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }

    size_t path_len = strlen(file_path) + 1;
    char *path_copy = (char *)malloc(path_len);
    if (!path_copy) {
        OSI_LOGE(TAG, "audio_resource: Failed to allocate memory for path");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -3);
    }
    memcpy(path_copy, file_path, path_len);

    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_FILE_MP3;
    msg.res_id = AUDIO_RES_MAX;
    msg.file_path = path_copy;
    msg.callback = callback;

    if (!osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: Failed to queue audio file");
        free(path_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -4);
    }

    OSI_LOGI(TAG, "audio_resource: Queued file=%s", path_copy);
    return 0;
}

/**
 * @brief Play an audio resource (using medium priority queue)
 */
int audio_resource_play_try_nothing_timeout(audio_resource_id_t res_id, audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, res_id, -1);
    }
    
    if (res_id >= AUDIO_RES_MAX) {
        OSI_LOGE(TAG, "audio_resource: Invalid resource ID %d", res_id);
        AUDIO_RETURN_ERROR(callback, res_id, -2);
    }
    
    /* Allocate memory for file path */
    char *file_path = (char *)malloc(256);
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Failed to allocate memory for path");
        AUDIO_RETURN_ERROR(callback, res_id, -3);
    }
    
    /* Build audio file path */
    int ret = prv_build_audio_path(res_id, file_path, 256);
    if (ret == -2) {
        /* Resource not provided by customer, skip playback silently */
        OSI_LOGD(TAG, "audio_resource: Resource %d not provided, skipping playback", res_id);
        free(file_path);
        if (callback) {
            callback(res_id, AUDIO_PLAYBACK_STATUS_SKIPPED, 0);
        }
        return 0;  /* Not an error */
    } else if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to build path for res_id=%d", res_id);
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, ret);
    }
    
    /* Prepare message */
    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_FILE_MP3;
    msg.res_id = res_id;
    msg.file_path = file_path;
    msg.callback = callback;
    
    /* Push to medium priority queue */
    if (!osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 0)) {
        OSI_LOGE(TAG, "audio_resource: Failed to queue audio message");
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, -4);
    }
    
    OSI_LOGI(TAG, "audio_resource: Queued res_id=%d, path=%s", res_id, file_path);
    return 0;
}

/**
 * @brief Play an audio resource with high priority (interrupt current playback)
 */
int audio_resource_play_high_priority(audio_resource_id_t res_id, audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, res_id, -1);
    }
    
    if (res_id >= AUDIO_RES_MAX) {
        OSI_LOGE(TAG, "audio_resource: Invalid resource ID %d", res_id);
        AUDIO_RETURN_ERROR(callback, res_id, -2);
    }
    
    /* Allocate memory for file path */
    char *file_path = (char *)malloc(256);
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Failed to allocate memory for path");
        AUDIO_RETURN_ERROR(callback, res_id, -3);
    }
    
    /* Build audio file path */
    int ret = prv_build_audio_path(res_id, file_path, 256);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to build path for res_id=%d", res_id);
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, ret);
    }
    
    /* Prepare message */
    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_FILE_MP3;
    msg.res_id = res_id;
    msg.file_path = file_path;
    msg.callback = callback;
    
    /* Push to high priority queue */
    if (!osiMessageQueueTryPut(s_manager.audio_queue_high, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: Failed to queue high priority audio");
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, -4);
    }
    
    OSI_LOGI(TAG, "audio_resource: Queued HIGH priority res_id=%d, path=%s", res_id, file_path);
    return 0;
}

int audio_resource_play_mid_priority(audio_resource_id_t res_id, audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, res_id, -1);
    }
    
    if (res_id >= AUDIO_RES_MAX) {
        OSI_LOGE(TAG, "audio_resource: Invalid resource ID %d", res_id);
        AUDIO_RETURN_ERROR(callback, res_id, -2);
    }
    
    /* Allocate memory for file path */
    char *file_path = (char *)malloc(256);
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Failed to allocate memory for path");
        AUDIO_RETURN_ERROR(callback, res_id, -3);
    }
    
    /* Build audio file path */
    int ret = prv_build_audio_path(res_id, file_path, 256);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to build path for res_id=%d", res_id);
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, ret);
    }
    
    /* Prepare message */
    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_FILE_MP3;
    msg.res_id = res_id;
    msg.file_path = file_path;
    msg.callback = callback;
    msg.tick = osiGetTicks();
    
    /* Push to medium priority queue */
    if (!osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: Failed to queue mid priority audio");
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, -4);
    }
    
    OSI_LOGI(TAG, "audio_resource: Queued HIGH priority res_id=%d, path=%s", res_id, file_path);
    return 0;
}

/**
 * @brief Play an audio resource with high priority (interrupt current playback)
 */
int audio_resource_play_with_priority(audio_resource_id_t res_id, audio_playback_callback_t callback, audio_play_pirority_e priority)
{
    bool res = false;
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, res_id, -1);
    }
    
    if (res_id >= AUDIO_RES_MAX) {
        OSI_LOGE(TAG, "audio_resource: Invalid resource ID %d", res_id);
        AUDIO_RETURN_ERROR(callback, res_id, -2);
    }
    
    /* Allocate memory for file path */
    char *file_path = (char *)malloc(256);
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Failed to allocate memory for path");
        AUDIO_RETURN_ERROR(callback, res_id, -3);
    }
    
    /* Build audio file path */
    int ret = prv_build_audio_path(res_id, file_path, 256);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to build path for res_id=%d", res_id);
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, ret);
    }
    
    /* Prepare message */
    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_FILE_MP3;
    msg.res_id = res_id;
    msg.file_path = file_path;
    msg.callback = callback;
    msg.tick = osiGetTicks();
    msg.stop_flag = false;
    
    /* Push to high priority queue */
    switch (priority)
    {
    case AUDIO_PLAY_PIRORITY_LOW:
        res = osiMessageQueueTryPut(s_manager.audio_queue_low, &msg, 1000);
        break;
    case AUDIO_PLAY_PIRORITY_MID:
        res = osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000);
        break;
    case AUDIO_PLAY_PIRORITY_HIGH:
        res = osiMessageQueueTryPut(s_manager.audio_queue_high, &msg, 1000);
        break;
    case AUDIO_PLAY_PIRORITY_URGENT:
        res = osiMessageQueueTryPut(s_manager.audio_queue_urgent, &msg, OS_WAIT_FORERVER);
        prv_audio_pa_codec_enable(false);
        mp3PlayStop(0xffffffff, 0);
        break;
    
    default:
        break;
    }
    if (!res) {
        OSI_LOGE(TAG, "audio_resource: Failed to queue high priority audio");
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, -4);
    }
    
    OSI_LOGI(TAG, "audio_resource: Queued HIGH priority res_id=%d, path=%s", res_id, file_path);
    return 0;
}

/**
 * @brief Play an audio resource with low priority (background tips)
 */
int audio_resource_play_low_priority(audio_resource_id_t res_id, audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, res_id, -1);
    }
    
    if (res_id >= AUDIO_RES_MAX) {
        OSI_LOGE(TAG, "audio_resource: Invalid resource ID %d", res_id);
        AUDIO_RETURN_ERROR(callback, res_id, -2);
    }
    
    /* Check if currently playing */
    if (osiSemaphoreTryAcquire(s_manager.play_semaphore, 0) == false) {
        OSI_LOGW(TAG, "audio_resource: Audio playing, skip low priority res_id=%u", res_id);
        AUDIO_RETURN_ERROR(callback, res_id, -4);  /* Busy */
    }
    osiSemaphoreRelease(s_manager.play_semaphore);
    
    /* Allocate memory for file path */
    char *file_path = (char *)malloc(256);
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Failed to allocate memory for path");
        AUDIO_RETURN_ERROR(callback, res_id, -3);
    }
    
    /* Build audio file path */
    int ret = prv_build_audio_path(res_id, file_path, 256);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to build path for res_id=%d", res_id);
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, ret);
    }
    
    /* Prepare message */
    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_FILE_MP3;
    msg.res_id = res_id;
    msg.file_path = file_path;
    msg.callback = callback;
    msg.tick = osiGetTicks();
    
    /* Push to low priority queue */
    if (!osiMessageQueueTryPut(s_manager.audio_queue_low, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: Failed to queue low priority audio");
        free(file_path);
        AUDIO_RETURN_ERROR(callback, res_id, -5);
    }
    
    OSI_LOGI(TAG, "audio_resource: Queued LOW priority res_id=%d, path=%s", res_id, file_path);
    return 0;
}

/**
 * @brief Play multiple audio resources as a stream
 */
int audio_resource_play_stream(const char *stream_buf, uint32_t stream_size, 
                                audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }

    if (!stream_buf || stream_size == 0) {
        OSI_LOGE(TAG, "audio_resource: invalid stream input");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -4);
    }

    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_STREAM_MP3;
    msg.stream_buf = malloc(stream_size);
    if (!msg.stream_buf) {
        OSI_LOGE(TAG, "audio_resource: stream buffer alloc failed");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -3);
    }
    memcpy(msg.stream_buf, stream_buf, stream_size);
    msg.stream_size = stream_size;
    msg.callback = callback;

    if (!osiMessageQueueTryPut(s_manager.audio_queue_high, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: stream queue full");
        free(msg.stream_buf);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }

    OSI_LOGI(TAG, "audio_resource: stream async queued");

    return 0;
}

int audio_resource_play_sequence_stream_async(audio_sequence_stream_builder_t builder, void *user_ctx,
                                             audio_playback_callback_t callback, audio_play_pirority_e priority)
{
    bool ret = false;
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_NOT_INITIALIZED);
    }

    if (!builder) {
        OSI_LOGE(TAG, "audio_resource: sequence builder missing");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_PARAM_INVALID);
    }

    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_SEQUENCE_STREAM_MP3;
    msg.seq_builder = builder;
    msg.seq_builder_ctx = user_ctx;
    msg.callback = callback;
    msg.tick = osiGetTicks();

    switch(priority)
    {
        case AUDIO_PLAY_PIRORITY_LOW:
            ret = osiMessageQueueTryPut(s_manager.audio_queue_low, &msg, 1000);
            break;
        case AUDIO_PLAY_PIRORITY_MID:
            ret = osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000);
            break;
        case AUDIO_PLAY_PIRORITY_HIGH:
            ret = osiMessageQueueTryPut(s_manager.audio_queue_high, &msg, 1000);
            break;
        case AUDIO_PLAY_PIRORITY_URGENT:
            ret = osiMessageQueueTryPut(s_manager.audio_queue_urgent, &msg, OS_WAIT_FORERVER);
            prv_audio_pa_codec_enable(false);
            mp3PlayStop(0xffffffff, 0);
            break;
        default:
            ret = osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000);
            break;
    }


    if (ret == false) {
        OSI_LOGE(TAG, "audio_resource: sequence stream queue full");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_QUEUE_FULL);
    }

    OSI_LOGI(TAG, "audio_resource: sequence stream async queued");
    return FRAMEWORK_AUDIO_SUCCESS;
}

static char *prv_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src) + 1;
    char *dst = malloc(len);
    if (!dst) {
        return NULL;
    }
    memcpy(dst, src, len);
    return dst;
}

int audio_resource_play_combo_async_mid_priority(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback, uint8_t timeout)
{
    uint8_t path = 0;

    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_NOT_INITIALIZED);
    }

    if (!folder || !file_list_str) {
        OSI_LOGE(TAG, "audio_resource: combo folder or list missing");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_PARAM_INVALID);
    }

    char *list_copy = prv_strdup(file_list_str);
    if (!list_copy) {
        OSI_LOGE(TAG, "audio_resource: combo list alloc failed");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_PARAM_INVALID);
    }

    if(strncmp(list_copy, "D:/", 3) == 0){
        path = 1;
    }

    const char *tokens[AUDIO_COMBO_MAX_FILES] = {0};
    uint32_t token_count = 0;
    char *cursor = list_copy;
    while (cursor && *cursor != '\0') {
        char *next = strchr(cursor, ',');
        if (next) {
            *next = '\0';
            next++;
        }
        char *trimmed = prv_trim_token(cursor);
        if (trimmed && *trimmed != '\0') {
            if (token_count >= AUDIO_COMBO_MAX_FILES) {
                OSI_LOGE(TAG, "audio_resource: combo entry limit %d exceeded", AUDIO_COMBO_MAX_FILES);
                free(list_copy);
                AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -4);
            }
            tokens[token_count++] = trimmed;
        }
        cursor = next;
    }

    if (token_count == 0) {
        OSI_LOGE(TAG, "audio_resource: combo list empty");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_PARAM_INVALID);
    }

    audio_combo_request_t *combo_req = malloc(sizeof(audio_combo_request_t));
    if (!combo_req) {
        OSI_LOGE(TAG, "audio_resource: combo request alloc failed");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_MEMORY_ERROR);
    }

    memset(combo_req, 0, sizeof(*combo_req));
    combo_req->source = AUDIO_COMBO_SOURCE_PATH;
    combo_req->count = token_count;
    combo_req->paths = malloc(token_count * sizeof(char *));
    if (!combo_req->paths) {
        OSI_LOGE(TAG, "audio_resource: combo path array alloc failed");
        free(list_copy);
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_MEMORY_ERROR);
    }

    size_t folder_len = strlen(folder);
    OSI_LOGI(TAG, "audio_resource: combo LANG='%s' folder=%s file_list='%s'",s_dynamic_language_prefix, folder, file_list_str);
    for (uint32_t i = 0; i < token_count; ++i) {
        size_t name_len = strlen(tokens[i]);
        size_t prefix_len = strlen(s_dynamic_language_prefix);
        size_t full_len = prefix_len + folder_len + name_len;
        char *full_path = malloc(full_len + 1);
        if (!full_path) {
            OSI_LOGE(TAG, "audio_resource: combo path alloc failed idx=%ld", i);
            free(list_copy);
            prv_free_combo_request(combo_req);
            AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_MEMORY_ERROR);
        }
        if(path){
            memcpy(full_path, folder, folder_len);
            memcpy(full_path + folder_len, tokens[i], name_len + 1);
        }else{
            memcpy(full_path, s_dynamic_language_prefix, prefix_len);
            memcpy(full_path + prefix_len, folder, folder_len);
            memcpy(full_path + prefix_len + folder_len, tokens[i], name_len + 1);
        }
        combo_req->paths[i] = full_path;
        full_path[full_len] = '\0';
    }

    free(list_copy);

    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_SEQUENCE_MP3;
    msg.res_id = AUDIO_RES_MAX;
    msg.combo_req = combo_req;
    msg.callback = callback;
    if(timeout){
        msg.tick = osiGetTicks();
    }

    if (!osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: combo queue full");
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_QUEUE_FULL);
    }

    return FRAMEWORK_AUDIO_SUCCESS;
}

int audio_resource_play_combo_async_mid_priority_TryPut(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback, uint8_t timeout)
{
    uint8_t path = 0;

    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_NOT_INITIALIZED);
    }

    if (!folder || !file_list_str) {
        OSI_LOGE(TAG, "audio_resource: combo folder or list missing");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_PARAM_INVALID);
    }

    char *list_copy = prv_strdup(file_list_str);
    if (!list_copy) {
        OSI_LOGE(TAG, "audio_resource: combo list alloc failed");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_PARAM_INVALID);
    }

    if(strncmp(list_copy, "D:/", 3) == 0){
        path = 1;
    }

    const char *tokens[AUDIO_COMBO_MAX_FILES] = {0};
    uint32_t token_count = 0;
    char *cursor = list_copy;
    while (cursor && *cursor != '\0') {
        char *next = strchr(cursor, ',');
        if (next) {
            *next = '\0';
            next++;
        }
        char *trimmed = prv_trim_token(cursor);
        if (trimmed && *trimmed != '\0') {
            if (token_count >= AUDIO_COMBO_MAX_FILES) {
                OSI_LOGE(TAG, "audio_resource: combo entry limit %d exceeded", AUDIO_COMBO_MAX_FILES);
                free(list_copy);
                AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -4);
            }
            tokens[token_count++] = trimmed;
        }
        cursor = next;
    }

    if (token_count == 0) {
        OSI_LOGE(TAG, "audio_resource: combo list empty");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_PARAM_INVALID);
    }

    audio_combo_request_t *combo_req = malloc(sizeof(audio_combo_request_t));
    if (!combo_req) {
        OSI_LOGE(TAG, "audio_resource: combo request alloc failed");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_MEMORY_ERROR);
    }

    memset(combo_req, 0, sizeof(*combo_req));
    combo_req->source = AUDIO_COMBO_SOURCE_PATH;
    combo_req->count = token_count;
    combo_req->paths = malloc(token_count * sizeof(char *));
    if (!combo_req->paths) {
        OSI_LOGE(TAG, "audio_resource: combo path array alloc failed");
        free(list_copy);
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_MEMORY_ERROR);
    }

    size_t folder_len = strlen(folder);
    OSI_LOGI(TAG, "audio_resource: combo LANG='%s' folder=%s file_list='%s'",s_dynamic_language_prefix, folder, file_list_str);
    for (uint32_t i = 0; i < token_count; ++i) {
        size_t name_len = strlen(tokens[i]);
        size_t prefix_len = strlen(s_dynamic_language_prefix);
        size_t full_len = prefix_len + folder_len + name_len;
        char *full_path = malloc(full_len + 1);
        if (!full_path) {
            OSI_LOGE(TAG, "audio_resource: combo path alloc failed idx=%ld", i);
            free(list_copy);
            prv_free_combo_request(combo_req);
            AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_MEMORY_ERROR);
        }
        if(path){
            memcpy(full_path, folder, folder_len);
            memcpy(full_path + folder_len, tokens[i], name_len + 1);
        }else{
            memcpy(full_path, s_dynamic_language_prefix, prefix_len);
            memcpy(full_path + prefix_len, folder, folder_len);
            memcpy(full_path + prefix_len + folder_len, tokens[i], name_len + 1);
        }
        combo_req->paths[i] = full_path;
        full_path[full_len] = '\0';
    }

    free(list_copy);

    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_SEQUENCE_MP3;
    msg.res_id = AUDIO_RES_MAX;
    msg.combo_req = combo_req;
    msg.callback = callback;
    if(timeout){
        msg.tick = osiGetTicks();
    }

    if (!osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 0)) {
        OSI_LOGE(TAG, "audio_resource: combo queue full");
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, FRAMEWORK_AUDIO_QUEUE_FULL);
    }

    return FRAMEWORK_AUDIO_SUCCESS;
}

int audio_resource_play_combo_async_mid_priority_stop(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback)
{
    uint8_t path = 0;

    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }

    if (!folder || !file_list_str) {
        OSI_LOGE(TAG, "audio_resource: combo folder or list missing");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }

    char *list_copy = prv_strdup(file_list_str);
    if (!list_copy) {
        OSI_LOGE(TAG, "audio_resource: combo list alloc failed");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -3);
    }

    if(strncmp(list_copy, "D:/", 3) == 0){
        path = 1;
    }

    const char *tokens[AUDIO_COMBO_MAX_FILES] = {0};
    uint32_t token_count = 0;
    char *cursor = list_copy;
    while (cursor && *cursor != '\0') {
        char *next = strchr(cursor, ',');
        if (next) {
            *next = '\0';
            next++;
        }
        char *trimmed = prv_trim_token(cursor);
        if (trimmed && *trimmed != '\0') {
            if (token_count >= AUDIO_COMBO_MAX_FILES) {
                OSI_LOGE(TAG, "audio_resource: combo entry limit %d exceeded", AUDIO_COMBO_MAX_FILES);
                free(list_copy);
                AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -4);
            }
            tokens[token_count++] = trimmed;
        }
        cursor = next;
    }

    if (token_count == 0) {
        OSI_LOGE(TAG, "audio_resource: combo list empty");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -5);
    }

    audio_combo_request_t *combo_req = malloc(sizeof(audio_combo_request_t));
    if (!combo_req) {
        OSI_LOGE(TAG, "audio_resource: combo request alloc failed");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -6);
    }

    memset(combo_req, 0, sizeof(*combo_req));
    combo_req->source = AUDIO_COMBO_SOURCE_PATH;
    combo_req->count = token_count;
    combo_req->paths = malloc(token_count * sizeof(char *));
    if (!combo_req->paths) {
        OSI_LOGE(TAG, "audio_resource: combo path array alloc failed");
        free(list_copy);
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -7);
    }

    size_t folder_len = strlen(folder);
    OSI_LOGI(TAG, "LANG='%s' folder=%s file_list='%s'",s_dynamic_language_prefix, folder, file_list_str);
    for (uint32_t i = 0; i < token_count; ++i) {
        size_t name_len = strlen(tokens[i]);
        size_t prefix_len = strlen(s_dynamic_language_prefix);
        size_t full_len = prefix_len + folder_len + name_len;
        char *full_path = malloc(full_len + 1);
        if (!full_path) {
            OSI_LOGE(TAG, "audio_resource: combo path alloc failed idx=%ld", i);
            free(list_copy);
            prv_free_combo_request(combo_req);
            AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -8);
        }
        if(path){
            memcpy(full_path, folder, folder_len);
            memcpy(full_path + folder_len, tokens[i], name_len + 1);
        }else{
            memcpy(full_path, s_dynamic_language_prefix, prefix_len);
            memcpy(full_path + prefix_len, folder, folder_len);
            memcpy(full_path + prefix_len + folder_len, tokens[i], name_len + 1);
        }
        combo_req->paths[i] = full_path;
        full_path[full_len] = '\0';
    }

    free(list_copy);

    audio_msg_t msg = {0};
    msg.type = AUDIO_MSG_TYPE_SEQUENCE_MP3;
    msg.res_id = AUDIO_RES_MAX;
    msg.combo_req = combo_req;
    msg.callback = callback;
    msg.stop_flag = 1;

    if (!osiMessageQueueTryPut(s_manager.audio_queue_mid, &msg, 1000)) {
        OSI_LOGE(TAG, "audio_resource: combo queue full");
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }

    return 0;
}

int audio_resource_play_combo_async(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback)
{
    uint8_t path = 0;

    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }

    if (!folder || !file_list_str) {
        OSI_LOGE(TAG, "audio_resource: combo folder or list missing");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }

    char *list_copy = prv_strdup(file_list_str);
    if (!list_copy) {
        OSI_LOGE(TAG, "audio_resource: combo list alloc failed");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -3);
    }

    if(strncmp(list_copy, "D:/", 3) == 0){
        path = 1;
    }

    const char *tokens[AUDIO_COMBO_MAX_FILES] = {0};
    uint32_t token_count = 0;
    char *cursor = list_copy;
    while (cursor && *cursor != '\0') {
        char *next = strchr(cursor, ',');
        if (next) {
            *next = '\0';
            next++;
        }
        char *trimmed = prv_trim_token(cursor);
        if (trimmed && *trimmed != '\0') {
            if (token_count >= AUDIO_COMBO_MAX_FILES) {
                OSI_LOGE(TAG, "audio_resource: combo entry limit %d exceeded", AUDIO_COMBO_MAX_FILES);
                free(list_copy);
                AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -4);
            }
            tokens[token_count++] = trimmed;
        }
        cursor = next;
    }

    if (token_count == 0) {
        OSI_LOGE(TAG, "audio_resource: combo list empty");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -5);
    }

    audio_combo_request_t *combo_req = malloc(sizeof(audio_combo_request_t));
    if (!combo_req) {
        OSI_LOGE(TAG, "audio_resource: combo request alloc failed");
        free(list_copy);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -6);
    }

    memset(combo_req, 0, sizeof(*combo_req));
    combo_req->source = AUDIO_COMBO_SOURCE_PATH;
    combo_req->count = token_count;
    combo_req->paths = malloc(token_count * sizeof(char *));
    if (!combo_req->paths) {
        OSI_LOGE(TAG, "audio_resource: combo path array alloc failed");
        free(list_copy);
        prv_free_combo_request(combo_req);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -7);
    }

    size_t folder_len = strlen(folder);
    OSI_LOGI(TAG, "audio_resource: combo LANG='%s' folder=%s file_list='%s'",s_dynamic_language_prefix, folder, file_list_str);
    for (uint32_t i = 0; i < token_count; ++i) {
        size_t name_len = strlen(tokens[i]);
        size_t prefix_len = strlen(s_dynamic_language_prefix);
        size_t full_len = prefix_len + folder_len + name_len;
        char *full_path = malloc(full_len + 1);
        if (!full_path) {
            OSI_LOGE(TAG, "audio_resource: combo path alloc failed idx=%lu", i);
            free(list_copy);
            prv_free_combo_request(combo_req);
            AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -8);
        }
        if(path){
            memcpy(full_path, folder, folder_len);
            memcpy(full_path + folder_len, tokens[i], name_len + 1);
        }else{
            memcpy(full_path, s_dynamic_language_prefix, prefix_len);
            memcpy(full_path + prefix_len, folder, folder_len);
            memcpy(full_path + prefix_len + folder_len, tokens[i], name_len + 1);
        }
        combo_req->paths[i] = full_path;
        full_path[full_len] = '\0';
    }

    free(list_copy);
    return prv_queue_combo_request(combo_req, callback, AUDIO_RES_MAX);
}

/**
 * @brief Stop current audio playback
 */
int audio_resource_stop(void)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        return -1;
    }

    s_manager.stop = true;
    
    /* Clear all queues and free allocated memory */
    // audio_msg_t msg;
    // while (osiMessageQueueTryGet(s_manager.audio_queue_high, &msg, 0)) {
    //     prv_free_audio_msg(&msg);
    // }
    // while (osiMessageQueueTryGet(s_manager.audio_queue_mid, &msg, 0)) {
    //     prv_free_audio_msg(&msg);
    // }
    // while (osiMessageQueueTryGet(s_manager.audio_queue_low, &msg, 0)) {
    //     prv_free_audio_msg(&msg);
    // }
    
    // /* Send stop message to high priority queue */
    // memset(&msg, 0, sizeof(msg));
    // msg.type = AUDIO_MSG_TYPE_STOP;
    // msg.res_id = AUDIO_RES_MAX;
    // msg.file_path = NULL;
    // msg.callback = NULL;
    // osiMessageQueueTryPut(s_manager.audio_queue_high, &msg, 100);
    
    // s_manager.current_callback = NULL;
    // s_manager.current_res_id = AUDIO_RES_MAX;
    // prv_release_stream_buffer();
    
    OSI_LOGI(TAG, "audio_resource: Stop requested");
    return 0;
}

/**
 * @brief Check if audio is currently playing
 */
bool audio_resource_is_playing(void)
{
    if (!s_manager.initialized) {
        return false;
    }
    
    return s_audio_is_playing;
}

/**
 * @brief Play a custom audio file by path
 */
int audio_resource_play_custom(const char *file_path, audio_playback_callback_t callback)
{
    if (!s_manager.initialized) {
        OSI_LOGE(TAG, "audio_resource: Not initialized");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -1);
    }
    
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Invalid file path");
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, -2);
    }
    
    /* Play the custom audio file */
    int ret = mp3PlayStart(file_path, NULL, 0);
    if (ret != 0) {
        OSI_LOGE(TAG, "audio_resource: Failed to play custom file %s", file_path);
        AUDIO_RETURN_ERROR(callback, AUDIO_RES_MAX, ret);
    }
    
    s_manager.current_callback = callback;
    s_manager.current_res_id = AUDIO_RES_MAX;
    
    OSI_LOGI(TAG, "audio_resource: Playing custom file %s", file_path);
    return 0;
}

/**
 * @brief Get resource ID from resource name
 */
audio_resource_id_t audio_resource_get_id_by_name(const char *name)
{
    if (!name) {
        return AUDIO_RES_MAX;
    }

    for (uint32_t i = 0; i < CUSTOMER_AUDIO_MAP_COUNT; i++) {
        /* Get filepath and extract filename for comparison */
        const char *filepath = CUSTOMER_AUDIO_MAP[i].filepath;
        if (!filepath) {
            continue;  /* Skip NULL entries */
        }
        
        /* Extract filename from full path (after last '/') */
        const char *filename = strrchr(filepath, '/');
        if (filename) {
            filename++;  /* Skip the '/' */
        } else {
            filename = filepath;  /* No path separator, use whole string */
        }
        
        if (strcmp(filename, name) == 0) {
            return CUSTOMER_AUDIO_MAP[i].id;
        }
    }

    return AUDIO_RES_MAX;
}/**
 * @brief Play audio prompt by file path
 * @note Stub implementation - to be implemented
 */
int framework_audio_play_prompt(const char *file_path)
{
    if (!file_path) {
        OSI_LOGE(TAG, "audio_resource: Invalid file path");
        return -1;
    }
    
    OSI_LOGI(TAG, "audio_resource: Play prompt: %s", file_path);
    // TODO: Implement actual playback
    return 0;
}

/**
 * @brief Switch to next track
 * @note Stub implementation - to be implemented
 */
void framework_audio_next_track(void)
{
    OSI_LOGI(TAG, "audio_resource: Next track");
    // TODO: Implement track switching
}

/**
 * @brief Switch to previous track
 * @note Stub implementation - to be implemented
 */
void framework_audio_prev_track(void)
{
    OSI_LOGI(TAG, "audio_resource: Previous track");
    // TODO: Implement track switching
}

/**
 * @brief Switch audio mode
 * @note Stub implementation - to be implemented
 */
void framework_audio_switch_mode(void)
{
    OSI_LOGI(TAG, "audio_resource: Switch mode");
    // TODO: Implement mode switching
}

/**
 * @brief Set the duration for which audio messages remain active
 * @note Stub implementation - to be implemented
 */
void framework_audio_valid_time(uint32_t time)
{
    s_audio_valid_time = time;
}

/**
 * @brief Check if a resource exists in the current language
 */
bool audio_resource_exists(audio_resource_id_t res_id)
{
    if (!s_manager.initialized) {
        return false;
    }
    
    if (res_id >= AUDIO_RES_MAX) {
        return false;
    }
    
    const char *filename = prv_find_resource_filename(res_id);
    if (!filename) {
        return false;
    }
    
    /* In a real implementation, you would check if the file exists on disk
     * For now, we just verify that the resource is mapped */
    return true;
}

/**
 * @brief Playback completion handler (to be called by audio system)
 * This should be registered with the audio playback system
 */
void audio_resource_playback_complete(int result)
{
    if (s_manager.current_callback) {
        audio_playback_status_t status = (result == 0) ? AUDIO_PLAYBACK_STATUS_ENDED : AUDIO_PLAYBACK_STATUS_ERROR;
        s_manager.current_callback(s_manager.current_res_id, status, result);
        s_manager.current_callback = NULL;
    }
    s_manager.current_res_id = AUDIO_RES_MAX;
}

#if defined(GAIN_V1)
    int gain_list[VOL_MAX] = {0xffffdc00,0xffffe0da,0xffffe5b3,0xffffea8c,0xffffef65,0xfffff43f,0xfffff918,0xfffffe6d};
#else
    int gain_list[VOL_MAX] = {0xffffe5b3,0xffffea8c,0xffffef65,0xfffff43f,0xfffff918,0xfffffe6d,0x000005b2,0x00000917};
#endif

void audio_volume_set(uint8_t vol){
    RTI_LOG("Audio set vol:%d", vol);
    // int gain_list[VOL_MAX] = {0xffffe24e,0xffffe3c2,0xffffe537,0xffffe6ab,0xffffe820,0xffffe994,0xffffeb08,0xffffec7d,0xffffee6d,0xffffefe1,0xfffff156,0xfffff2ca};
    //int gain_list[VOL_MAX] = {0xffffe727,0xffffe918,0xffffeb84,0xffffedf1,0xfffff05e,0xfffff2ca,0xfffff537,0xfffff820,0xfffffa8c,0xfffffb08,0xfffffc7d,0xfffffe6d};
                            //   0          1           2           3           4       5           6           7
    if(vol >= VOL_MAX){
        vol = VOL_MAX - 1;
    }

    auPlaySoftGainSet(gain_list[vol]);
    volume = vol;

#ifdef CUSTOMER_SOUNDBOX_SD_MODULE
    /* When SD module build enables this feature: mute at volume level 1 */
    if (vol <= 1) {
        prv_audio_pa_codec_enable(false);
    } else {
        prv_audio_pa_codec_enable(true);
    }
#endif
}

int framework_audio_set_volume_gain_list(int *new_gain_list,int num)
{
    if(new_gain_list == NULL || num > VOL_MAX){
        return -1;
    }
    memcpy(gain_list, new_gain_list, sizeof(int) * num);
    return 0;
}

uint8_t audio_volume_get(void)
{
    return volume;
}

int framework_audio_stop_mid_priority(void)
{
    if(!s_audio_stop_enable){
        return 0;
    }

    mp3PlayStop(0xffffffff, 0);
    prv_audio_pa_codec_enable(false);

    return 1;
}
