/**
 * @file framework_main.c
 * @brief Framework entry point
 */

#include "framework_msg.h"
#include "framework_battery.h"
#include "framework_led.h"
#include "framework_log.h"
#include "framework_led.h"
#include "framework_key.h"
#include "framework_audio.h"
#include "framework_network.h"
#include "framework_mqtt.h"
#include "framework_http.h"
#include "framework_unzip.h"
#include "framework_device_config.h"
#include "framework_charger.h"
#include "framework_factory.h"
#include "framework_config.h"
#include "app_version.h"
#include <stdbool.h>

#define TAG "MAIN"

void customer_factory_init(void);

extern void customer_init(void);
extern void osiSysPoweroff(void);
extern int appVersionSet(const char *version);
#if FRAMEWORK_POWEROFF_CHARGER_ENABLE
int customer_poweroff_charge_init(void);
static bool prv_check_powweroff_charge(void)
{
    uint32_t flag = 0;
    uint8_t poweron_reason = framework_key_get_poweron_reason();
    OSI_LOGI(TAG, "Power on reason: %d", poweron_reason);
    framework_device_get_poweroff_flag(&flag);
    if (poweron_reason == 2 || flag == FRAMEWORK_POWER_OFF_FLAG_CHARGER) {
        OSI_LOGI(TAG, "Power off due to charger disconnect");
        return true;
    }
    OSI_LOGI(TAG, "Power off due to other reason");
    return false;
}
#endif
/**
 * @brief Application entry point (called by soundbox.c)
 */
void app_main(void)
{

    OSI_LOGI(TAG, "=== System Boot ===");
    OSI_LOGI(TAG, "=== version: %s ===", APP_VERSION_STRING);
    OSI_LOGI(TAG, "=== Git Hash: %s ===", APP_GIT_COMMIT_HASH);

    appVersionSet(APP_VERSION_STRING);

    /* Step 3: Normal boot - Initialize framework */
    OSI_LOGI(TAG, "=== Framework Initialize ===");

    // Initialize framework components
    framework_msg_init();
    #ifdef FRAMEWORK_ENABLE_LED
    framework_led_init();
    #endif
    framework_key_init();

    // Initialize audio resource manager
    audio_resource_config_t audio_config = {
        .default_language = AUDIO_LANG_VIETNAMESE,
        .max_path_length = 256,
        .auto_fallback = true
    };
    int ret = audio_resource_manager_init(&audio_config);
    if (ret != 0) {
        OSI_LOGE(TAG, "Audio resource manager init failed: %d", ret);
    } else {
        OSI_LOGI(TAG, "Audio resource manager initialized");
    }

    framework_factory_init();
    framework_device_config_init();
    if (framework_factory_check_mode() == FACTORY_MODE_DETECTED) {
        OSI_LOGI(TAG, "Factory mode detected - entering factory test");
        framework_factory_enter(NULL);
        /* Factory mode loop - would implement factory test logic here */
        customer_factory_init();
        while (1) {
            osiThreadSleep(50);  /* 1 second */
        }
        return;
    }

    #if FRAMEWORK_POWEROFF_CHARGER_ENABLE
    if(prv_check_powweroff_charge()){
        customer_poweroff_charge_init();
        return;
    }
    #endif

    audio_volume_set(framework_device_config_get_volume());

    /* Initialize network management */
    framework_network_init();

    OSI_LOGI(TAG, "Framework initialized successfully");

    // Customer initialization would be called here
    customer_init();

    framework_battery_init();

    /* Initialize HTTP module (20KB stack for async requests) */
    ret = framework_http_init();
    if (ret != 0) {
        OSI_LOGE(TAG, ": HTTP module init failed: %d", ret);
    } else {
        OSI_LOGI(TAG, ": HTTP module initialized (20KB stack)");
    }

    ret = framework_unzip_init();
    if (ret != 0) {
        OSI_LOGE(TAG, ": Unzip module init failed: %d", ret);
    } else {
        OSI_LOGI(TAG, ": Unzip module initialized");
    }
    
    /* Start network connection (MQTT will auto-start after PDP activation) */
    OSI_LOGI(TAG, "Starting network connection...");
    framework_network_start();

}/**
 * @brief Weak audio manager stubs (to be implemented later)
 */
__attribute__((weak)) void audio_manager_volume_up(void) 
{
    OSI_LOGI(TAG, "TODO: audio_manager_volume_up");
}

__attribute__((weak)) void audio_manager_volume_down(void)
{
    OSI_LOGI(TAG, "TODO: audio_manager_volume_down");
}

/**
 * @brief Weak customer init stub (to be implemented by customer)
 */
__attribute__((weak)) void customer_init(void)
{
    OSI_LOGI(TAG, "TODO: customer_init");
}

__attribute__((weak)) void customer_factory_init(void)
{
    OSI_LOGI(TAG, "TODO: customer_factory_init");
}
/**
 * @brief Weak customer poweroff charge init stub (to be implemented by customer)
 */
__attribute__((weak)) int customer_poweroff_charge_init(void)
{
    return -1;
}
