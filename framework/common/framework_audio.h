#ifndef AUDIO_COMBO_MAX_FILES
#define AUDIO_COMBO_MAX_FILES 32
#endif
/**
 * @file audio_resource_manager.h
 * @brief Audio Resource Manager for ZxInfotek Soundbox
 * @version 1.0
 * @date 2025-11-20
 * 
 * Audio resource management system supporting multi-language audio playback
 * for system prompts, volume indicators, battery status, network status,
 * payment confirmations, and bank notifications.
 */

#ifndef __FRAMEWORK_AUDIO_H__
#define __FRAMEWORK_AUDIO_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio language definitions
 */
typedef enum {
    AUDIO_LANG_VIETNAMESE = 0,  /**< Vietnamese language */
    AUDIO_LANG_ENGLISH = 1,     /**< English language */
    AUDIO_LANG_MAX              /**< Maximum language count */
} audio_language_t;

typedef enum {
    FRAMEWORK_AUDIO_SUCCESS = 0,
    FRAMEWORK_AUDIO_NOT_INITIALIZED = -1,
    FRAMEWORK_AUDIO_PARAM_INVALID = -2,
    FRAMEWORK_AUDIO_QUEUE_FULL = -3,
    FRAMEWORK_AUDIO_MEMORY_ERROR = -4,
    FRAMEWORK_AUDIO_ERROR_CODES_MAX = -16,
} framework_audio_error_codes_e;

/**
 * @brief System audio resource IDs
 */
typedef enum {
    /* System Prompts */
    AUDIO_RES_SYS_STARTUP = 0,          /**< System startup sound */
    AUDIO_RES_SYS_SHUTDOWN,             /**< System shutdown sound */
    AUDIO_RES_SYS_READY,                /**< System ready prompt */
    AUDIO_RES_SYS_ERROR,                /**< System error prompt */
    AUDIO_RES_SYS_SUCCESS,              /**< Operation success */
    AUDIO_RES_SYS_FAILURE,              /**< Operation failure */
    AUDIO_RES_SYS_TIMEOUT,              /**< Operation timeout */
    AUDIO_RES_SYS_CANCEL,               /**< Operation cancelled */
    AUDIO_RES_SYS_CONFIRM,              /**< Confirmation prompt */
    AUDIO_RES_SYS_DENY,                 /**< Denial prompt */
    
    /* Volume Indicators (10-19) */
    AUDIO_RES_VOL_MIN = 10,             /**< Volume minimum */
    AUDIO_RES_VOL_LOW,                  /**< Volume low */
    AUDIO_RES_VOL_MEDIUM,               /**< Volume medium */
    AUDIO_RES_VOL_HIGH,                 /**< Volume high */
    AUDIO_RES_VOL_MAX,                  /**< Volume maximum */
    AUDIO_RES_VOL_MUTE,                 /**< Volume muted */
    AUDIO_RES_VOL_UNMUTE,               /**< Volume unmuted */
    AUDIO_RES_VOL_UP,                   /**< Volume up */
    AUDIO_RES_VOL_DOWN,                 /**< Volume down */
    
    /* Battery Status (20-29) */
    AUDIO_RES_BAT_FULL = 20,            /**< Battery full */
    AUDIO_RES_BAT_HIGH,                 /**< Battery high (75-99%) */
    AUDIO_RES_BAT_MEDIUM,               /**< Battery medium (25-74%) */
    AUDIO_RES_BAT_LOW,                  /**< Battery low (10-24%) */
    AUDIO_RES_BAT_CRITICAL,             /**< Battery critical (<10%) */
    AUDIO_RES_BAT_CHARGING,             /**< Battery charging */
    AUDIO_RES_BAT_NOT_CHARGING,         /**< Battery not charging */
    AUDIO_RES_BAT_CHARGE_COMPLETE,      /**< Charging complete */
    
    /* Charging Status (30-39) */
    AUDIO_RES_CHG_CONNECTED = 30,       /**< Charger connected */
    AUDIO_RES_CHG_DISCONNECTED,         /**< Charger disconnected */
    AUDIO_RES_CHG_FAST_CHARGING,        /**< Fast charging mode */
    AUDIO_RES_CHG_SLOW_CHARGING,        /**< Slow charging mode */
    AUDIO_RES_CHG_ERROR,                /**< Charging error */
    AUDIO_RES_CHG_OVERHEATING,          /**< Overheating warning */
    
    /* Network Status (40-59) */
    AUDIO_RES_NET_CONNECTED = 40,       /**< Network connected */
    AUDIO_RES_NET_DISCONNECTED,         /**< Network disconnected */
    AUDIO_RES_NET_CONNECTING,           /**< Network connecting */
    AUDIO_RES_NET_WIFI_ON,              /**< WiFi turned on */
    AUDIO_RES_NET_WIFI_OFF,             /**< WiFi turned off */
    AUDIO_RES_NET_WIFI_WEAK,            /**< WiFi signal weak */
    AUDIO_RES_NET_WIFI_STRONG,          /**< WiFi signal strong */
    AUDIO_RES_NET_4G_ON,                /**< 4G network on */
    AUDIO_RES_NET_4G_OFF,               /**< 4G network off */
    AUDIO_RES_NET_BT_CONNECTED,         /**< Bluetooth connected */
    AUDIO_RES_NET_BT_DISCONNECTED,      /**< Bluetooth disconnected */
    AUDIO_RES_NET_BT_PAIRING,           /**< Bluetooth pairing */
    AUDIO_RES_NET_NO_SIGNAL,            /**< No network signal */
    AUDIO_RES_NET_ROAMING,              /**< Network roaming */
    AUDIO_RES_NET_SERVER_ERROR,         /**< Server connection error */
    
    /* Money Amount Prompts (60-99) */
    AUDIO_RES_MONEY_RECEIVED = 60,      /**< Money received prompt */
    AUDIO_RES_MONEY_SENT,               /**< Money sent prompt */
    AUDIO_RES_MONEY_1000,               /**< 1,000 VND */
    AUDIO_RES_MONEY_2000,               /**< 2,000 VND */
    AUDIO_RES_MONEY_5000,               /**< 5,000 VND */
    AUDIO_RES_MONEY_10000,              /**< 10,000 VND */
    AUDIO_RES_MONEY_20000,              /**< 20,000 VND */
    AUDIO_RES_MONEY_50000,              /**< 50,000 VND */
    AUDIO_RES_MONEY_100000,             /**< 100,000 VND */
    AUDIO_RES_MONEY_200000,             /**< 200,000 VND */
    AUDIO_RES_MONEY_500000,             /**< 500,000 VND */
    AUDIO_RES_MONEY_1000000,            /**< 1,000,000 VND */
    AUDIO_RES_MONEY_2000000,            /**< 2,000,000 VND */
    AUDIO_RES_MONEY_5000000,            /**< 5,000,000 VND */
    AUDIO_RES_MONEY_10000000,           /**< 10,000,000 VND */
    AUDIO_RES_MONEY_THOUSAND,           /**< Thousand unit */
    AUDIO_RES_MONEY_MILLION,            /**< Million unit */
    AUDIO_RES_MONEY_BILLION,            /**< Billion unit */
    AUDIO_RES_MONEY_DONG,               /**< Vietnamese Dong */
    
    /* Bank Names (100-149) */
    AUDIO_RES_BANK_VIETTEL = 100,       /**< Viettel Money */
    AUDIO_RES_BANK_VIETCOMBANK,         /**< Vietcombank */
    AUDIO_RES_BANK_AGRIBANK,            /**< Agribank */
    AUDIO_RES_BANK_BIDV,                /**< BIDV */
    AUDIO_RES_BANK_TECHCOMBANK,         /**< Techcombank */
    AUDIO_RES_BANK_MBBANK,              /**< MB Bank */
    AUDIO_RES_BANK_ACB,                 /**< ACB */
    AUDIO_RES_BANK_SACOMBANK,           /**< Sacombank */
    AUDIO_RES_BANK_VPBANK,              /**< VPBank */
    AUDIO_RES_BANK_TPBANK,              /**< TPBank */
    AUDIO_RES_BANK_HDBANK,              /**< HDBank */
    AUDIO_RES_BANK_VIETINBANK,          /**< VietinBank */
    AUDIO_RES_BANK_SHB,                 /**< SHB */
    AUDIO_RES_BANK_EXIMBANK,            /**< Eximbank */
    AUDIO_RES_BANK_MSB,                 /**< MSB */
    AUDIO_RES_BANK_OCB,                 /**< OCB */
    AUDIO_RES_BANK_SEABANK,             /**< SeABank */
    AUDIO_RES_BANK_VIETBANK,            /**< VietBank */
    AUDIO_RES_BANK_PVCOMBANK,           /**< PVcomBank */
    AUDIO_RES_BANK_BAOVIETBANK,         /**< BaoViet Bank */
    AUDIO_RES_BANK_MOMO,                /**< MoMo */
    AUDIO_RES_BANK_ZALOPAY,             /**< ZaloPay */
    AUDIO_RES_BANK_VNPAY,               /**< VNPAY */
    AUDIO_RES_BANK_VIETTELPAY,          /**< ViettelPay */
    AUDIO_RES_BANK_AIRPAY,              /**< AirPay */
    
    /* Payment Status (150-169) */
    AUDIO_RES_PAY_SUCCESS = 150,        /**< Payment successful */
    AUDIO_RES_PAY_FAILED,               /**< Payment failed */
    AUDIO_RES_PAY_PENDING,              /**< Payment pending */
    AUDIO_RES_PAY_CANCELLED,            /**< Payment cancelled */
    AUDIO_RES_PAY_TIMEOUT,              /**< Payment timeout */
    AUDIO_RES_PAY_INSUFFICIENT,         /**< Insufficient balance */
    AUDIO_RES_PAY_INVALID,              /**< Invalid transaction */
    AUDIO_RES_PAY_PROCESSING,           /**< Processing payment */
    AUDIO_RES_PAY_REFUNDED,             /**< Payment refunded */
    
    /* QR Code Status (170-179) */
    AUDIO_RES_QR_SCAN = 170,            /**< Scan QR code */
    AUDIO_RES_QR_VALID,                 /**< Valid QR code */
    AUDIO_RES_QR_INVALID,               /**< Invalid QR code */
    AUDIO_RES_QR_EXPIRED,               /**< QR code expired */
    AUDIO_RES_QR_GENERATED,             /**< QR code generated */
    
    /* Numbers 0-9 (180-189) */
    AUDIO_RES_NUM_0 = 180,              /**< Number 0 */
    AUDIO_RES_NUM_1,                    /**< Number 1 */
    AUDIO_RES_NUM_2,                    /**< Number 2 */
    AUDIO_RES_NUM_3,                    /**< Number 3 */
    AUDIO_RES_NUM_4,                    /**< Number 4 */
    AUDIO_RES_NUM_5,                    /**< Number 5 */
    AUDIO_RES_NUM_6,                    /**< Number 6 */
    AUDIO_RES_NUM_7,                    /**< Number 7 */
    AUDIO_RES_NUM_8,                    /**< Number 8 */
    AUDIO_RES_NUM_9,                    /**< Number 9 */
    
    /* Additional System (190-199) */
    AUDIO_RES_SYS_UPDATE = 190,         /**< System update */
    AUDIO_RES_SYS_RESET,                /**< System reset */
    AUDIO_RES_SYS_CONFIG,               /**< System configuration */
    AUDIO_RES_SYS_TEST_MODE,            /**< Test mode */
    AUDIO_RES_SYS_FACTORY_RESET,        /**< Factory reset */
    
    AUDIO_RES_MAX                       /**< Maximum resource ID */
} audio_resource_id_t;

/**
 * @brief Audio playback status codes
 */
typedef enum {
    AUDIO_PLAYBACK_STATUS_IDLE = 0,     /**< No playback active */
    AUDIO_PLAYBACK_STATUS_STARTED = 1,  /**< Playback started */
    AUDIO_PLAYBACK_STATUS_ENDED = 2,    /**< Playback finished normally */
    AUDIO_PLAYBACK_STATUS_SKIPPED = 3,  /**< Playback skipped or pre-empted */
    AUDIO_PLAYBACK_STATUS_ERROR = -1    /**< Playback error */
} audio_playback_status_t;

typedef enum {
    AUDIO_PLAY_PIRORITY_LOW = 0,     /**< Normal playback priority */
    AUDIO_PLAY_PIRORITY_MID = 1,     /**< Normal playback priority */
    AUDIO_PLAY_PIRORITY_HIGH = 2,       /**< High playback priority */
    AUDIO_PLAY_PIRORITY_URGENT = 3      /**< Urgent playback priority , push to highest priority front*/
}audio_play_pirority_e;

/**
 * @brief Audio resource manager configuration
 */
typedef struct {
    audio_language_t default_language;  /**< Default language used on init */
    uint32_t max_path_length;           /**< Max file path length */
    bool auto_fallback;                 /**< Fallback to other languages on missing file */
} audio_resource_config_t;

/**
 * @brief Audio playback callback
 * @param res_id Resource ID
 * @param status Playback status
 * @param result Result code (0 on success, negative on error)
 */
typedef void (*audio_playback_callback_t)(audio_resource_id_t res_id, audio_playback_status_t status, int result);

/**
 * @brief Callback to build an MP3 stream on demand.
 * @param user_ctx Caller-provided context pointer
 * @param out_buf Buffer provided by audio thread to write MP3 data into
 * @param buf_size Size of out_buf in bytes
 * @param out_size Written MP3 size on success
 * @return 0 on success, negative on error
 */
typedef int (*audio_sequence_stream_builder_t)(void *user_ctx, char *out_buf, uint32_t buf_size, uint32_t *out_size);

/**
 * @brief Initialize audio resource manager
 *
 * @param config Configuration structure (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int audio_resource_manager_init(const audio_resource_config_t *config);

/**
 * @brief Deinitialize audio resource manager
 *
 * @return 0 on success, negative error code on failure
 */
int audio_resource_manager_deinit(void);

/**
 * @brief Set current audio language
 * 
 * @param language Language to set
 * @return 0 on success, negative error code on failure
 */
int audio_resource_set_language(audio_language_t language);

/**
 * @brief Get current audio language
 * 
 * @return Current language setting
 */
audio_language_t audio_resource_get_language(void);

/**
 * @brief Get audio file path for a resource ID
 * 
 * @param res_id Resource ID
 * @param path_buffer Buffer to store path
 * @param buffer_size Size of buffer
 * @return 0 on success, negative error code on failure
 */
int audio_resource_get_path(audio_resource_id_t res_id, char *path_buffer, uint32_t buffer_size);

/**
 * @brief Play an audio resource
 * 
 * @param res_id Resource ID to play
 * @param callback Optional callback when playback completes (can be NULL)
 * @return 0 on success, negative error code on failure
 */
int audio_resource_play(audio_resource_id_t res_id, audio_playback_callback_t callback);

/**
 * @brief Play an audio file by absolute path (no language prefix added)
 *
 * @param file_path Full path to the audio file
 * @param callback Optional callback when playback completes (can be NULL)
 * @return 0 on success, negative error code on failure
 */
int audio_file_play(const char *file_path, audio_playback_callback_t callback);

/**
 * @brief Play audio stream from memory buffer
 * @param stream_buf Pointer to audio stream buffer
 * @param stream_size Size of audio stream buffer in bytes
 * @param callback Optional callback when playback completes (can be NULL)
 * @return 0 on success, negative error code on failure
 */
int audio_resource_play_stream(const char *stream_buf, uint32_t stream_size, 
                                audio_playback_callback_t callback);

/**
 * @brief Queue sequence playback where MP3 data is built inside the audio thread via builder callback
 *
 * @param builder Builder invoked in audio thread to fill provided buffer with MP3 data
 * @param user_ctx Opaque context passed to builder
 * @param callback Optional playback callback
 * @return 0 on success, negative on error
 */
int audio_resource_play_sequence_stream_async(audio_sequence_stream_builder_t builder, void *user_ctx,
                                             audio_playback_callback_t callback, audio_play_pirority_e priority);
/**
 * @brief Queue asynchronous combo playback by specifying a folder prefix and comma-separated filenames
 *
 * @param folder Folder prefix (e.g., "D:/ENG/")
 * @param file_list_str Comma-separated list of filenames within the folder (e.g., "num1.mp3,num2.mp3")
 * @param callback Optional playback callback (can be NULL)
 * @return 0 on success, negative value on error
 */
int audio_resource_play_combo_async(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback);

/**
 * @brief Queue asynchronous combo playback by specifying a folder prefix and comma-separated filenames mid priority
 *
 * @param folder Folder prefix (e.g., "D:/ENG/")
 * @param file_list_str Comma-separated list of filenames within the folder (e.g., "num1.mp3,num2.mp3")
 * @param callback Optional playback callback (can be NULL)
 * @param timeout >0 enable timeout, 0 disable timeout
 * @return 0 on success, negative value on error
 */
int audio_resource_play_combo_async_mid_priority(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback, uint8_t timeout);

/**
 * @brief Play a custom audio file by absolute path (no language prefix added)
 */
int audio_resource_play_custom(const char *file_path, audio_playback_callback_t callback);

                                    /**
 * @brief Stop current audio playback
 * 
 * @return 0 on success, negative error code on failure
 */
int audio_resource_stop(void);

/**
 * @brief Check if audio is currently playing
 * 
 * @return true if playing, false otherwise
 */
bool audio_resource_is_playing(void);

/**
 * @brief Get resource ID from resource name
 *
 * @param name Resource name string
 * @return Resource ID or AUDIO_RES_MAX if not found
 */
audio_resource_id_t audio_resource_get_id_by_name(const char *name);

/**
 * @brief Check if a resource exists in the current language
 *
 * @param res_id Resource ID
 * @return true if exists, false otherwise
 */
bool audio_resource_exists(audio_resource_id_t res_id);

/**
 * @brief Play an audio resource with high priority (interrupt current playback)
 * @param res_id Audio resource ID
 * @param callback Playback completion callback
 * @return 0 on success, negative on error
 */
int audio_resource_play_high_priority(audio_resource_id_t res_id, audio_playback_callback_t callback);

/**
 * @brief Play an audio resource with mid priority (same queue as default helper, explicit API)
 * @param res_id Audio resource ID
 * @param callback Playback completion callback
 * @return 0 on success, negative on error
 */
int audio_resource_play_mid_priority(audio_resource_id_t res_id, audio_playback_callback_t callback);

/**
 * @brief Play an audio resource with low priority (background tips)
 * @param res_id Audio resource ID
 * @param callback Playback completion callback 
 * @return 0 on success, negative on error
 */
int audio_resource_play_low_priority(audio_resource_id_t res_id, audio_playback_callback_t callback);

/**
 * @brief Play an audio resource with specified priority
 * @param res_id Audio resource ID
 * @param callback Playback completion callback
 * @param priority Playback priority
 * @return 0 on success, negative on error
 */
int audio_resource_play_with_priority(audio_resource_id_t res_id, audio_playback_callback_t callback, audio_play_pirority_e priority);

/* ==================================================================
 * Extended Audio Framework APIs
 * ================================================================ *//**
 * @brief Play audio prompt by file path
 * @param file_path Full path to audio file
 * @return 0 on success, negative on error
 */
int framework_audio_play_prompt(const char *file_path);

/**
 * @brief Switch to next track
 */
void framework_audio_next_track(void);

/**
 * @brief Switch to previous track
 */
void framework_audio_prev_track(void);

/**
 * @brief Switch audio mode (e.g., music/radio/bluetooth)
 */
void framework_audio_switch_mode(void);

/**
 * @brief Play battery status announcement
 */
void framework_audio_play_battery_status(void);

/**
 * @brief Set audio volume
 * @param vol Volume level (0-10)
 */
void audio_volume_set(uint8_t vol);


/**
 * @brief Get current audio volume
 * @return Volume level (0-10)
 */
uint8_t audio_volume_get(void);

/**
 * @brief Set language prefix for audio resources
 * @param prefix Language folder prefix (e.g., "D:/ENG/")
 */
void audio_resource_set_language_prefix(const char *prefix);

/**
 * @brief Get language prefix for audio resources
 * @param prefix Language folder prefix (e.g., "D:/ENG/")
 */
char* audio_resource_get_language_prefix(void);

/**
 * @brief Set volume gain list
 * @param new_gain_list Array of gain values
 * @param num Number of gain values
 * @return 0 on success, negative on error
 */
int framework_audio_set_volume_gain_list(int *new_gain_list,int num);


/**
 * @brief Check if audio is currently playing
 * 
 * @return true if playing, false otherwise
 */
bool audio_resource_is_playing(void);

/**
 * @brief Set the validity time for audio playback
 * @details Configure the time duration for which the audio remains valid
 * @param time Duration in milliseconds for audio validity
 */
void framework_audio_valid_time(uint32_t time);

int framework_audio_stop_mid_priority(void);
int audio_resource_play_combo_async_mid_priority_stop(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback);
int audio_resource_play_combo_async_mid_priority_TryPut(const char *folder, const char *file_list_str,
                                    audio_playback_callback_t callback, uint8_t timeout);
int audio_resource_play_try_nothing_timeout(audio_resource_id_t res_id, audio_playback_callback_t callback);
#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_RESOURCE_MANAGER_H__ */
