/**
 * @file framework_factory.c
 * @brief Factory mode detection and test implementation
 */

#include "framework_factory.h"
#include "framework_log.h"
#include "framework_msg.h"
#include "framework_key.h"
#include "framework_led.h"
#include "framework_audio.h"
#include "framework_config.h"
#include "fs_api.h"
#include "osi_api.h"
#include <stdint.h>
#include <stdbool.h>

/* Include factory AT command system */
#include "../factory/factory_at.h"
#include "../factory/factory_at_cmds.h"

#define TAG "FACTORY"

#define FM_FACTORY_MODE_FILE  "D:/.factory_mode"

/* Factory mode state */
static struct {
    bool active;                        /**< Factory mode active flag */
    factory_mode_callback_t callback;   /**< Entry callback */
} g_factory_ctx = {
    .active = false,
    .callback = NULL
};

/**
 * @brief Initialize factory mode detection
 */
int framework_factory_init(void)
{
    OSI_LOGI(TAG, "Factory mode detector initialized");
    g_factory_ctx.active = false;
    g_factory_ctx.callback = NULL;
    return 0;
}

/**
 * @brief Check if should enter factory mode
 * @details Check for specific key combination or flag
 */
__attribute__((weak)) factory_mode_result_t framework_factory_check_mode(void)
{
    /* TODO: Check factory mode detection conditions
     * Examples:
     * 1. Check specific GPIO state at boot
     * 2. Check for specific key combination held during power-on
     * 3. Check flash flag set by PC tool
     * 4. Check USB enumeration state
     */
    
    // For now, check if specific keys are pressed
    // This can be customized based on hardware requirements
    if(fs_size(FM_FACTORY_MODE_FILE) > 0){
        OSI_LOGI(TAG, "Factory mode check: Detected via flag file");
        return FACTORY_MODE_DETECTED;
    }
    
    OSI_LOGI(TAG, "Factory mode check: Not detected");
    return FACTORY_MODE_NONE;
}

/**
 * @brief Enter factory mode
 */
int framework_factory_enter(factory_mode_callback_t callback)
{
    if (g_factory_ctx.active) {
        OSI_LOGW(TAG, "Already in factory mode");
        return -1;
    }
    
    OSI_LOGI(TAG, "Entering factory mode");
    
    g_factory_ctx.active = true;
    g_factory_ctx.callback = callback;
    
    /* Initialize AT command system for factory testing */
    OSI_LOGI(TAG, "Initializing factory AT commands");
    factory_at_cmds_init();
    
    /* Play factory mode prompt if available */
    // audio_resource_play(AUDIO_RES_SYS_FACTORY_MODE, NULL);
    
    /* Call entry callback if provided */
    if (callback) {
        callback();
    }
    
    OSI_LOGI(TAG, "Factory mode active - AT commands ready");
    return 0;
}

/**
 * @brief Check if currently in factory mode
 */
bool framework_factory_is_active(void)
{
    return g_factory_ctx.active;
}

/**
 * @brief Factory mode message processing
 */
void framework_factory_process_message(uint32_t msg_id, void *param)
{
    if (!g_factory_ctx.active) {
        return;
    }
    
    /* Handle factory mode messages */
    /* TODO: Define factory-specific message IDs */
    OSI_LOGI(TAG, "Factory mode message: %d", msg_id);
}
