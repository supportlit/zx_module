/**
 * @file framework_device_config.h
 * @brief Framework Device Configuration Management Module
 * @details Manages device SN, volume and other persistent configurations
 */

#ifndef __FRAMEWORK_DEVICE_CONFIG_H__
#define __FRAMEWORK_DEVICE_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_SN_MAX_LEN       64    /**< Maximum device SN length */
#define DEVICE_NAME_MAX_LEN     128   /**< Maximum device name length */
#define MQTT_PRODUCT_KEY_LEN    64    /**< MQTT Product Key length */
#define MQTT_DEVICE_SECRET_LEN  128   /**< MQTT Device Secret length */
#define MQTT_CLIENT_ID_LEN      128   /**< MQTT Client ID length */

/**
 * @brief MQTT triple information (stored in user_cfg partition)
 */
typedef struct {
    uint32_t magic;                                   /**< Magic code for validation */
    char device_sn[DEVICE_SN_MAX_LEN];                /**< Device serial number */
    char device_name[DEVICE_NAME_MAX_LEN];            /**< Device name */
    char product_key[MQTT_PRODUCT_KEY_LEN];           /**< MQTT Product Key */
    char device_secret[MQTT_DEVICE_SECRET_LEN];       /**< MQTT Device Secret */
    char client_id[MQTT_CLIENT_ID_LEN];               /**< MQTT Client ID */
    uint8_t reserved[256];                            /**< Reserved for future use */
} framework_mqtt_config_t;

typedef enum {
    FRAMEWORK_POWER_OFF_FLAG_NORMAL = 0,
    FRAMEWORK_POWER_OFF_FLAG_CHARGER = 1,
} framework_power_off_flag_e;

/**
 * @brief Volume configuration (stored in file)
 */
typedef struct {
    uint32_t magic;       /**< Magic code for validation */
    uint8_t volume;       /**< Current volume level */
    uint8_t reserved[32]; /**< Reserved for future use */
} framework_volume_config_t;

/**
 * @brief External flash types as stored in sys_cfg partition
 */
typedef enum {
    FRAMEWORK_EXT_FLASH_NOR = 0,
    FRAMEWORK_EXT_FLASH_NAND = 1,
    FRAMEWORK_EXT_FLASH_NOR_DUL = 2,
    FRAMEWORK_EXT_FLASH_UNKNOWN = 0xFF,
} framework_ext_flash_type_e;

/**
 * @brief Initialize device configuration module
 * @details Reads configuration from user_cfg partition with backup support
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_init(void);

/**
 * @brief Get device volume
 * @return Current volume level
 */
uint8_t framework_device_config_get_volume(void);

/**
 * @brief Set device volume
 * @param volume Volume level to set
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_set_volume(uint8_t volume);

/**
 * @brief Get device serial number
 * @param sn Buffer to store SN (must be at least DEVICE_SN_MAX_LEN bytes)
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_get_sn(char *sn, uint32_t max_len);

/**
 * @brief Set device serial number
 * @param sn Serial number to set
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_set_sn(const char *sn);

/**
 * @brief Get device name
 * @param name Buffer to store name (must be at least DEVICE_NAME_MAX_LEN bytes)
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_get_name(char *name, uint32_t max_len);

/**
 * @brief Set device name
 * @param name Device name to set
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_set_name(const char *name);

/**
 * @brief Get MQTT Product Key
 * @param product_key Buffer to store product key
 * @param max_len Maximum buffer length
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_get_product_key(char *product_key, uint32_t max_len);

/**
 * @brief Set MQTT Product Key
 * @param product_key Product key to set
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_set_product_key(const char *product_key);

/**
 * @brief Get MQTT Device Secret
 * @param device_secret Buffer to store device secret
 * @param max_len Maximum buffer length
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_get_device_secret(char *device_secret, uint32_t max_len);

/**
 * @brief Set MQTT Device Secret
 * @param device_secret Device secret to set
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_set_device_secret(const char *device_secret);

/**
 * @brief Get MQTT Client ID
 * @param client_id Buffer to store client ID
 * @param max_len Maximum buffer length
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_get_client_id(char *client_id, uint32_t max_len);

/**
 * @brief Set MQTT Client ID
 * @param client_id Client ID to set
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_set_client_id(const char *client_id);

/**
 * @brief Reset device configuration to defaults
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_reset(void);

/**
 * @brief Save configuration immediately
 * @details Saves volume to file and MQTT config to user_cfg partition
 * @return 0 on success, negative error code on failure
 */
int framework_device_config_save(void);

/**
 * @brief Initialize device factory module
 * @details Reads configuration from user_cfg partition with backup support
 * @return 0 on Normal mode, 1 Factory mode detected
 */
int framework_device_factory_check(void);

/**
 * @brief Set power off flag
 * @param flag Power off flag to set
 */
void framework_device_set_poweroff_flag(uint32_t flag);

/**
 * @brief Clear power off flag
 */
void framework_device_clear_poweroff_flag(void);

/**
 * @brief Get power off flag
 * @param flag Pointer to store retrieved flag
 * @return true if flag exists and retrieved, false otherwise
 */
bool framework_device_get_poweroff_flag(uint32_t *flag);
#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_DEVICE_CONFIG_H__ */
