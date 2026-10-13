/**
 * @file framework_device_config.c
 * @brief Framework Device Configuration Management Implementation
 * @details Implements volume file storage and MQTT config partition storage
 *          Volume saved to file: D:/volume.cfg
 *          MQTT triple saved to: user_cfg partition
 */

#include "framework_device_config.h"
#include "framework_log.h"
#include "framework_config.h"
#include "framework_audio.h"
#include "flash_api.h"
#include "fs_api.h"
#include "osi_api.h"
#include "osi_compiler.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define TAG                     "FM_CONFIG"

#define FRAMEWORK_POWER_OFF_FLAG  "D:/.POWEROFF_CFG"

#define USER_CFG_PTABLE         "user_cfg"
#define USER_CFG_BAK_NAME       "D:/user_cfg_bak.txt"
#define USER_CFG_SIZE           0x00001000  /* 4K */
#define VOLUME_CFG_PATH         "D:/volume.cfg"
#define VOLUME_CFG_SIZE         64          /* Volume config file size */

#define CFG_MAGIC_CODE          (0x46524D43)  /* "FRMC" - Framework Config */
#define VOL_MAGIC_CODE          (0x564F4C43)  /* "VOLC" - Volume Config */

#define SYS_CFG_PTABLE          "sys_cfg"
#define SYS_CFG_MAGIC           (0x53434647)  /* "SCFG" */
#define SYS_CFG_VERSION         (0x0001)
#define SYS_CFG_ENTRY_COUNT     (32)
#define SYS_CFG_ENTRY_SIZE      (12)
#define SYS_CFG_DATA_NO_CRC_LEN (12 + (SYS_CFG_ENTRY_COUNT * 2 * SYS_CFG_ENTRY_SIZE) + 8) /* header + no_usb + usb_in + ext flash size/num */
#define SYS_CFG_TOTAL_LEN       (SYS_CFG_DATA_NO_CRC_LEN + 4)

typedef struct {
    uint32_t reg;
    uint32_t action; /* 1=set bit, 0=clean bit ,2=set reg*/
    uint32_t val;
} sys_cfg_io_entry_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t ext_flash_type;
    sys_cfg_io_entry_t no_usb[SYS_CFG_ENTRY_COUNT];
    sys_cfg_io_entry_t usb_in[SYS_CFG_ENTRY_COUNT];
    uint32_t ext_flash_size;  // 每个flash芯片大小
    uint32_t ext_flash_num;   // flash芯片数量(DUL NOR USED)
    uint32_t crc32;
} sys_cfg_image_t;

OSI_UNUSED static uint32_t prv_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}
/* External flash operations */
extern unsigned int spi_nor_do_read(unsigned int addr, unsigned int buf_addr, unsigned int size, bool flashType);
extern int spi_nor_do_erase_4k(unsigned int addr, unsigned int size, bool flashType);
extern unsigned int spi_nor_do_write(unsigned int addr, unsigned int buf_addr, unsigned int size, bool flashType);

static framework_mqtt_config_t g_mqtt_config = {0};
static framework_volume_config_t g_volume_config = {0};
static bool g_initialized = false;
static bool g_volume_save_pending = false;
static bool g_mqtt_save_pending = false;
static osiThread_t *g_save_thread = NULL;

/* Forward declarations */
static int prv_read_user_cfg(uint8_t *buf);
static int prv_write_user_cfg(uint8_t *buf);
static int prv_read_user_cfg_with_backup(uint8_t *buf);
static int prv_write_user_cfg_with_backup(uint8_t *buf);
static int prv_read_volume_config(void);
static int prv_write_volume_config(void);
static void prv_save_task(void *param);
static bool prv_validate_mqtt_config(const framework_mqtt_config_t *config);
static bool prv_validate_volume_config(const framework_volume_config_t *config);

/**
 * @brief Read from user_cfg partition
 */
static int prv_read_user_cfg(uint8_t *buf)
{
    int ret = 0;
    uint32_t addr = 0;
    size_t size = 0;

    addr = flash_read_addr(USER_CFG_PTABLE);
    if (0 == addr) {
        OSI_LOGE(TAG, "device_config: Failed to find user_cfg partition");
        return -1;
    }

    size = flash_read_size(USER_CFG_PTABLE);
    if (size < sizeof(framework_mqtt_config_t)) {
        OSI_LOGE(TAG, "device_config: Partition size too small: %u", size);
        return -2;
    }

    ret = spi_nor_do_read(addr, (unsigned int)buf, size, 0);
    if (ret != 0) {
        OSI_LOGE(TAG, "device_config: Flash read failed: %d", ret);
        return -3;
    }

    return 0;
}

/**
 * @brief Write to user_cfg partition
 */
static int prv_write_user_cfg(uint8_t *buf)
{
    int ret = 0;
    uint32_t addr = 0;
    size_t size = 0;

    addr = flash_read_addr(USER_CFG_PTABLE);
    if (0 == addr) {
        OSI_LOGE(TAG, "device_config: Failed to find user_cfg partition");
        return -1;
    }

    size = flash_read_size(USER_CFG_PTABLE);
    
    /* Erase sector */
    ret = spi_nor_do_erase_4k(addr, size, 0);
    if (ret != 0) {
        OSI_LOGE(TAG, "device_config: Flash erase failed: %d", ret);
        return -2;
    }

    /* Write data */
    ret = spi_nor_do_write(addr, (unsigned int)buf, size, 0);
    if (ret != 0) {
        OSI_LOGE(TAG, "device_config: Flash write failed: %d", ret);
        return -3;
    }

    return 0;
}

/**
 * @brief Read user config with backup support
 */
static int prv_read_user_cfg_with_backup(uint8_t *buf)
{
    int ret = 0;
    int fd = 0;
    size_t size = 0;
    size_t config_size = flash_read_size(USER_CFG_PTABLE);

    if (NULL == buf || (config_size % USER_CFG_SIZE) != 0) {
        OSI_LOGE(TAG, "device_config: Invalid parameters");
        return -1;
    }

    /* Check if backup file exists */
    ret = fs_size(USER_CFG_BAK_NAME);
    if (ret <= 0) {
        /* No backup, read from flash directly */
        OSI_LOGI(TAG, "device_config: No backup file, reading from flash");
        ret = prv_read_user_cfg(buf);
        if (ret != 0) {
            OSI_LOGE(TAG, "device_config: Failed to read from flash: %d", ret);
            return -2;
        }
    } else {
        /* Backup exists, restore from backup */
        OSI_LOGI(TAG, "device_config: Restoring from backup file");
        
        fd = fs_open(USER_CFG_BAK_NAME, "rb");
        if (0 == fd) {
            OSI_LOGE(TAG, "device_config: Failed to open backup file");
            return -3;
        }

        size = fs_read(buf, sizeof(uint8_t), config_size, fd);
        fs_close(fd);

        if (size != config_size) {
            OSI_LOGE(TAG, "device_config: Backup file read error: %u/%u", size, config_size);
            return -4;
        }

        /* Write backup data to flash */
        ret = prv_write_user_cfg(buf);
        if (ret != 0) {
            OSI_LOGE(TAG, "device_config: Failed to restore backup to flash: %d", ret);
            return -5;
        }

        /* Remove backup file after successful restore */
        fs_remove(USER_CFG_BAK_NAME);
        OSI_LOGI(TAG, "device_config: Backup restored successfully");
    }

    return 0;
}

/**
 * @brief Write user config with backup support
 */
static int prv_write_user_cfg_with_backup(uint8_t *buf)
{
    int ret = 0;
    int fd = 0;
    size_t size = 0;
    size_t config_size = flash_read_size(USER_CFG_PTABLE);

    if (NULL == buf || (config_size % USER_CFG_SIZE) != 0) {
        OSI_LOGE(TAG, "device_config: Invalid parameters");
        return -1;
    }

    /* Create backup file first */
    fd = fs_open(USER_CFG_BAK_NAME, "wb+");
    if (0 == fd) {
        OSI_LOGE(TAG, "device_config: Failed to create backup file");
        return -2;
    }

    size = fs_write(buf, sizeof(uint8_t), config_size, fd);
    fs_close(fd);

    if (size != config_size) {
        OSI_LOGE(TAG, "device_config: Backup file write error: %u/%u", size, config_size);
        fs_remove(USER_CFG_BAK_NAME);
        return -3;
    }

    /* Write to flash */
    ret = prv_write_user_cfg(buf);
    if (ret != 0) {
        OSI_LOGE(TAG, "device_config: Flash write failed: %d", ret);
        /* Keep backup file for recovery */
        return -4;
    }

    /* Remove backup file after successful write */
    fs_remove(USER_CFG_BAK_NAME);
    
    return 0;
}

/**
 * @brief Validate MQTT configuration data
 */
static bool prv_validate_mqtt_config(const framework_mqtt_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    /* Check magic code */
    if (config->magic != CFG_MAGIC_CODE) {
        OSI_LOGI(TAG, "device_config: Invalid MQTT magic code: 0x%08lX", config->magic);
        return false;
    }

    return true;
}

/**
 * @brief Validate volume configuration data
 */
static bool prv_validate_volume_config(const framework_volume_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    /* Check magic code */
    if (config->magic != VOL_MAGIC_CODE) {
        OSI_LOGI(TAG, "device_config: Invalid volume magic code: 0x%08lX", config->magic);
        return false;
    }

    /* Validate volume range */
    if (config->volume > VOL_MAX) {
        OSI_LOGI(TAG, "device_config: Invalid volume: %d", config->volume);
        return false;
    }

    return true;
}

/**
 * @brief Read volume configuration from file
 */
static int prv_read_volume_config(void)
{
    int fd = 0;
    int ret = 0;
    int file_size = 0;

    file_size = fs_size(VOLUME_CFG_PATH);
    if (file_size <= 0) {
        OSI_LOGI(TAG, "device_config: Volume config file not found, using defaults");
        return -1;
    }

    fd = fs_open(VOLUME_CFG_PATH, "rb");
    if (fd <= 0) {
        OSI_LOGE(TAG, "device_config: Failed to open volume config file");
        return -2;
    }

    ret = fs_read(&g_volume_config, 1, sizeof(framework_volume_config_t), fd);
    fs_close(fd);

    if (ret != sizeof(framework_volume_config_t)) {
        OSI_LOGE(TAG, "device_config: Volume config file read error: %d", ret);
        return -3;
    }

    return 0;
}

/**
 * @brief Write volume configuration to file
 */
static int prv_write_volume_config(void)
{
    int fd = 0;
    int ret = 0;

    /* Wait for audio playback to complete */
    while (audio_resource_is_playing()) {
        osiThreadSleep(200 / 5);
    }

    fd = fs_open(VOLUME_CFG_PATH, "wb+");
    if (fd <= 0) {
        OSI_LOGE(TAG, "device_config: Failed to create volume config file");
        return -1;
    }

    ret = fs_write(&g_volume_config, 1, sizeof(framework_volume_config_t), fd);
    fs_close(fd);

    if (ret != sizeof(framework_volume_config_t)) {
        OSI_LOGE(TAG, "device_config: Volume config file write error: %d", ret);
        return -2;
    }

    OSI_LOGI(TAG, "device_config: Volume config saved successfully");
    return 0;
}

/**
 * @brief Save task thread (handles delayed saves)
 */
static void prv_save_task(void *param)
{
    uint8_t *buf = NULL;
    int ret = 0;
    size_t config_size = 0;

    OSI_LOGI(TAG, "device_config: Save task started");

    config_size = flash_read_size(USER_CFG_PTABLE);
    if (config_size == 0) {
        OSI_LOGE(TAG, "device_config: Failed to get partition size");
        return;
    }

    buf = (uint8_t *)malloc(config_size);
    if (buf == NULL) {
        OSI_LOGE(TAG, "device_config: Failed to allocate buffer");
        return;
    }

    while (1) {
        /* Handle volume config save */
        if (g_volume_save_pending) {
            g_volume_save_pending = false;

            /* Wait for audio playback to complete */
            while (audio_resource_is_playing()) {
                osiThreadSleep(200 / 5);
            }

            ret = prv_write_volume_config();
            if (ret != 0) {
                OSI_LOGE(TAG, "device_config: Failed to save volume config: %d", ret);
            }
        }

        /* Handle MQTT config save */
        if (g_mqtt_save_pending) {
            g_mqtt_save_pending = false;

            /* Wait for audio playback to complete */
            while (audio_resource_is_playing()) {
                osiThreadSleep(200 / 5);
            }

            OSI_LOGI(TAG, "device_config: Saving MQTT config to flash");

            memset(buf, 0xFF, config_size);
            memcpy(buf, &g_mqtt_config, sizeof(framework_mqtt_config_t));

            ret = prv_write_user_cfg_with_backup(buf);
            if (ret != 0) {
                OSI_LOGE(TAG, "device_config: Failed to save MQTT config: %d", ret);
            } else {
                OSI_LOGI(TAG, "device_config: MQTT config saved successfully");
            }
        }

        /* Sleep for 4 seconds */
        osiThreadSleep(1000 / 5);
    }

    free(buf);
}

/**
 * @brief Initialize device configuration module
 */
int framework_device_config_init(void)
{
    uint8_t *buf = NULL;
    int ret = 0;
    size_t config_size = 0;

    if (g_initialized) {
        OSI_LOGI(TAG, "device_config: Already initialized");
        return 0;
    }

    OSI_LOGI(TAG, "device_config: Initializing");

    /* Initialize volume configuration */
    ret = prv_read_volume_config();
    if (ret != 0 || !prv_validate_volume_config(&g_volume_config)) {
        OSI_LOGI(TAG, "device_config: Invalid volume config, resetting to defaults");
        memset(&g_volume_config, 0, sizeof(framework_volume_config_t));
        g_volume_config.magic = VOL_MAGIC_CODE;
        g_volume_config.volume = DEFAULT_VOL;
        g_volume_save_pending = true;
    }

    /* Initialize MQTT configuration from user_cfg partition */
    config_size = flash_read_size(USER_CFG_PTABLE);
    if (config_size == 0) {
        OSI_LOGE(TAG, "device_config: Failed to get partition size");
        return -1;
    }

    buf = (uint8_t *)malloc(config_size);
    if (buf == NULL) {
        OSI_LOGE(TAG, "device_config: Failed to allocate buffer");
        return -2;
    }

    memset(buf, 0xFF, config_size);

    /* Read MQTT configuration with backup support */
    ret = prv_read_user_cfg_with_backup(buf);
    if (ret != 0) {
        OSI_LOGE(TAG, "device_config: Failed to read MQTT config: %d", ret);
        free(buf);
        return -3;
    }

    /* Copy to global MQTT config */
    memcpy(&g_mqtt_config, buf, sizeof(framework_mqtt_config_t));
    free(buf);

    /* Validate MQTT configuration */
    if (!prv_validate_mqtt_config(&g_mqtt_config)) {
        OSI_LOGI(TAG, "device_config: Invalid MQTT config, resetting to defaults");
        memset(&g_mqtt_config, 0, sizeof(framework_mqtt_config_t));
        g_mqtt_config.magic = CFG_MAGIC_CODE;
        strcpy(g_mqtt_config.device_sn, "UNKNOWN");
        strcpy(g_mqtt_config.device_name, "ZXINFOTEK_SOUNDBOX");
        g_mqtt_save_pending = true;
    }

    /* Create save task thread */
    g_save_thread = osiThreadCreate("devCfgSave", prv_save_task, NULL, 120, 4 * 1024);
    if (g_save_thread == NULL) {
        OSI_LOGE(TAG, "device_config: Failed to create save thread");
        return -4;
    }

    g_initialized = true;

    OSI_LOGI(TAG, "device_config: Initialized - vol=%d, sn=%s",
             g_volume_config.volume, g_mqtt_config.device_sn);

    return 0;
}

/**
 * @brief Get device volume
 */
uint8_t framework_device_config_get_volume(void)
{
    if (!g_initialized) {
        OSI_LOGI(TAG, "device_config: Not initialized");
        return DEFAULT_VOL;
    }

    return g_volume_config.volume;
}

/**
 * @brief Set device volume
 */
int framework_device_config_set_volume(uint8_t volume)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (volume > VOL_MAX) {
        OSI_LOGE(TAG, "device_config: Invalid volume: %d", volume);
        return -2;
    }

    if (g_volume_config.volume == volume) {
        return 0;  /* No change */
    }

    g_volume_config.volume = volume;
    g_volume_save_pending = true;

    OSI_LOGI(TAG, "device_config: Volume updated to %d", volume);
    return 0;
}

/**
 * @brief Get device serial number
 */
int framework_device_config_get_sn(char *sn, uint32_t max_len)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (sn == NULL || max_len == 0) {
        OSI_LOGE(TAG, "device_config: Invalid parameters");
        return -2;
    }

    strncpy(sn, g_mqtt_config.device_sn, max_len - 1);
    sn[max_len - 1] = '\0';

    return 0;
}

/**
 * @brief Set device serial number
 */
int framework_device_config_set_sn(const char *sn)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (sn == NULL) {
        OSI_LOGE(TAG, "device_config: Invalid SN");
        return -2;
    }

    if (strcmp(g_mqtt_config.device_sn, sn) == 0) {
        return 0;  /* No change */
    }

    strncpy(g_mqtt_config.device_sn, sn, DEVICE_SN_MAX_LEN - 1);
    g_mqtt_config.device_sn[DEVICE_SN_MAX_LEN - 1] = '\0';
    
    g_mqtt_save_pending = true;

    OSI_LOGI(TAG, "device_config: SN updated to %s", sn);
    return 0;
}

/**
 * @brief Get device name
 */
int framework_device_config_get_name(char *name, uint32_t max_len)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (name == NULL || max_len == 0) {
        OSI_LOGE(TAG, "device_config: Invalid parameters");
        return -2;
    }

    strncpy(name, g_mqtt_config.device_name, max_len - 1);
    name[max_len - 1] = '\0';

    return 0;
}

/**
 * @brief Set device name
 */
int framework_device_config_set_name(const char *name)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (name == NULL) {
        OSI_LOGE(TAG, "device_config: Invalid name");
        return -2;
    }

    if (strcmp(g_mqtt_config.device_name, name) == 0) {
        return 0;  /* No change */
    }

    strncpy(g_mqtt_config.device_name, name, DEVICE_NAME_MAX_LEN - 1);
    g_mqtt_config.device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';
    
    g_mqtt_save_pending = true;

    OSI_LOGI(TAG, "device_config: Name updated to %s", name);
    return 0;
}

/**
 * @brief Get MQTT Product Key
 */
int framework_device_config_get_product_key(char *product_key, uint32_t max_len)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (product_key == NULL || max_len == 0) {
        OSI_LOGE(TAG, "device_config: Invalid parameters");
        return -2;
    }

    strncpy(product_key, g_mqtt_config.product_key, max_len - 1);
    product_key[max_len - 1] = '\0';

    return 0;
}

/**
 * @brief Set MQTT Product Key
 */
int framework_device_config_set_product_key(const char *product_key)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (product_key == NULL) {
        OSI_LOGE(TAG, "device_config: Invalid product key");
        return -2;
    }

    if (strcmp(g_mqtt_config.product_key, product_key) == 0) {
        return 0;  /* No change */
    }

    strncpy(g_mqtt_config.product_key, product_key, MQTT_PRODUCT_KEY_LEN - 1);
    g_mqtt_config.product_key[MQTT_PRODUCT_KEY_LEN - 1] = '\0';
    
    g_mqtt_save_pending = true;

    OSI_LOGI(TAG, "device_config: Product key updated");
    return 0;
}

/**
 * @brief Get MQTT Device Secret
 */
int framework_device_config_get_device_secret(char *device_secret, uint32_t max_len)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (device_secret == NULL || max_len == 0) {
        OSI_LOGE(TAG, "device_config: Invalid parameters");
        return -2;
    }

    strncpy(device_secret, g_mqtt_config.device_secret, max_len - 1);
    device_secret[max_len - 1] = '\0';

    return 0;
}

/**
 * @brief Set MQTT Device Secret
 */
int framework_device_config_set_device_secret(const char *device_secret)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (device_secret == NULL) {
        OSI_LOGE(TAG, "device_config: Invalid device secret");
        return -2;
    }

    if (strcmp(g_mqtt_config.device_secret, device_secret) == 0) {
        return 0;  /* No change */
    }

    strncpy(g_mqtt_config.device_secret, device_secret, MQTT_DEVICE_SECRET_LEN - 1);
    g_mqtt_config.device_secret[MQTT_DEVICE_SECRET_LEN - 1] = '\0';
    
    g_mqtt_save_pending = true;

    OSI_LOGI(TAG, "device_config: Device secret updated");
    return 0;
}

/**
 * @brief Get MQTT Client ID
 */
int framework_device_config_get_client_id(char *client_id, uint32_t max_len)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (client_id == NULL || max_len == 0) {
        OSI_LOGE(TAG, "device_config: Invalid parameters");
        return -2;
    }

    strncpy(client_id, g_mqtt_config.client_id, max_len - 1);
    client_id[max_len - 1] = '\0';

    return 0;
}

/**
 * @brief Set MQTT Client ID
 */
int framework_device_config_set_client_id(const char *client_id)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    if (client_id == NULL) {
        OSI_LOGE(TAG, "device_config: Invalid client ID");
        return -2;
    }

    if (strcmp(g_mqtt_config.client_id, client_id) == 0) {
        return 0;  /* No change */
    }

    strncpy(g_mqtt_config.client_id, client_id, MQTT_CLIENT_ID_LEN - 1);
    g_mqtt_config.client_id[MQTT_CLIENT_ID_LEN - 1] = '\0';
    
    g_mqtt_save_pending = true;

    OSI_LOGI(TAG, "device_config: Client ID updated");
    return 0;
}

/**
 * @brief Reset device configuration to defaults
 */
int framework_device_config_reset(void)
{
    OSI_LOGI(TAG, "device_config: Resetting to defaults");

    /* Reset volume config */
    memset(&g_volume_config, 0, sizeof(framework_volume_config_t));
    g_volume_config.magic = VOL_MAGIC_CODE;
    g_volume_config.volume = DEFAULT_VOL;
    g_volume_save_pending = true;

    /* Reset MQTT config */
    memset(&g_mqtt_config, 0, sizeof(framework_mqtt_config_t));
    g_mqtt_config.magic = CFG_MAGIC_CODE;
    strcpy(g_mqtt_config.device_sn, "UNKNOWN");
    strcpy(g_mqtt_config.device_name, "ZXINFOTEK_SOUNDBOX");
    g_mqtt_save_pending = true;

    return 0;
}

/**
 * @brief Save configuration immediately
 */
int framework_device_config_save(void)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    g_volume_save_pending = true;
    g_mqtt_save_pending = true;

    OSI_LOGI(TAG, "device_config: Immediate save requested");
    return 0;
}


/**
 * @brief Save configuration immediately
 */
int framework_device_factory_check(void)
{
    if (!g_initialized) {
        OSI_LOGE(TAG, "device_config: Not initialized");
        return -1;
    }

    /* Validate MQTT configuration */
    if (memcmp(g_mqtt_config.device_sn , "UNKNOWN", 7) == 0) {
        OSI_LOGI(TAG, "device_config: Invalid MQTT config, resetting to defaults");
        return 1;
    }

    OSI_LOGI(TAG, "device_config: Immediate save requested");
    return 0;
}

void framework_device_set_poweroff_flag(uint32_t flag){
    OSI_LOGI(TAG, "device_config: Set power off flag: %u", flag);
    int fd = fs_open(FRAMEWORK_POWER_OFF_FLAG, "wb+");
    fs_write(&flag, 1, sizeof(flag), fd);
    fs_close(fd);
}

void framework_device_clear_poweroff_flag(void){
    OSI_LOGI(TAG, "device_config: Clear power off flag");
    fs_remove(FRAMEWORK_POWER_OFF_FLAG);
}

bool framework_device_get_poweroff_flag(uint32_t *flag){
    int fsize = fs_size(FRAMEWORK_POWER_OFF_FLAG);

    if(fsize <= 0 || fsize != sizeof(uint32_t)){
        OSI_LOGI(TAG, "device_config: Power off flag file not found or invalid size: %d", fsize);
        return false;
    }
    int fd = fs_open(FRAMEWORK_POWER_OFF_FLAG, "rb");
    int ret = fs_read(flag, 1, sizeof(uint32_t), fd);
    fs_close(fd);
    if(ret != sizeof(uint32_t)){
        OSI_LOGI(TAG, "device_config: Get power off flag failed, ret: %d", ret);
        return false;
    }
    OSI_LOGI(TAG, "device_config: Get power off flag: %u", *flag);
    return true;
}