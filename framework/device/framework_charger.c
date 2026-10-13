/**
 * @file framework_charger.c
 * @brief Charger detection and fake power-on implementation
 */

#include "framework_charger.h"
#include "framework_log.h"
#include "framework_led.h"
#include "framework_key.h"
#include "framework_battery.h"
#include "osi_api.h"

#define TAG "CHARGER"

/* External functions from BSP */
extern bool CommPMVbusDetect(void);
extern void osiSysPoweroff(void);

/* Power-on state flags */
static struct {
    poweron_reason_t reason;
    bool initialized;
} g_charger_ctx = {
    .reason = POWERON_REASON_UNKNOWN,
    .initialized = false
};

/* Fake power-on control */
static osiMessageQueue_t *g_charger_queue = NULL;
static volatile bool g_exit_fake_mode = false;

/**
 * @brief Power key callback for fake power-on mode
 */
static void prv_fake_poweron_key_callback(powkey_event_e event, void *param)
{
    uint8_t msg = 0;
    
    if (event == POWKEY_EVENT_LONG_PRESS) {
        /* Long press - exit to normal mode */
        msg = 2;
        OSI_LOGI(TAG, "Fake mode: Power key long press - exit to app");
    } else if (event == POWKEY_EVENT_PRESS) {
        /* Short press - could show battery level */
        msg = 1;
        OSI_LOGI(TAG, "Fake mode: Power key short press");
    }
    
    if (g_charger_queue) {
        osiMessageQueueTryPut(g_charger_queue, &msg, 0);
    }
}

/**
 * @brief Fake power-on monitoring task
 */
static void prv_fake_poweron_task(void *param)
{
    uint8_t msg[1] = {0};
    
    OSI_LOGI(TAG, "Fake power-on monitoring task started");
    
    while (!g_exit_fake_mode) {
        /* Check USB connection */
        if (!CommPMVbusDetect()) {
            OSI_LOGI(TAG, "USB disconnected - powering off");
            g_exit_fake_mode = true;
            osiSysPoweroff();
            break;
        }
        
        /* Check message queue for key events */
        if (g_charger_queue && osiMessageQueueTryGet(g_charger_queue, msg, 50)) {
            if (msg[0] == 2) {
                /* Long press - exit fake mode */
                OSI_LOGI(TAG, "Exiting fake power-on mode");
                g_exit_fake_mode = true;
                break;
            } else if (msg[0] == 1) {
                /* Short press - flash LED to show battery status */
                framework_led_set_state(LED_STATE_CHARGE_FULL);
                osiThreadSleep(5);  /* 100ms flash */
                framework_led_set_state(LED_STATE_CHARGING);
            }
        }
        
        osiThreadSleep(20);  /* 100ms */
    }
    
    OSI_LOGI(TAG, "Fake power-on monitoring task exiting");
    osiThreadExit();
}

/**
 * @brief Initialize charger detection
 */
int framework_charger_init(void)
{
    OSI_LOGI(TAG, "Charger detection initialized");
    
    /* Determine power-on reason */
    /* TODO: Read from hardware register or flash flag */
    if (CommPMVbusDetect()) {
        g_charger_ctx.reason = POWERON_REASON_CHARGER;
        OSI_LOGI(TAG, "Power-on reason: CHARGER");
    } else {
        g_charger_ctx.reason = POWERON_REASON_KEY;
        OSI_LOGI(TAG, "Power-on reason: KEY");
    }
    
    g_charger_ctx.initialized = true;
    return 0;
}

/**
 * @brief Get power-on reason
 */
poweron_reason_t framework_charger_get_poweron_reason(void)
{
    return g_charger_ctx.reason;
}

/**
 * @brief Handle charger boot logic
 */
charger_boot_result_t framework_charger_handle_boot(void)
{
    if (!g_charger_ctx.initialized) {
        framework_charger_init();
    }
    
    /* If powered on by charger/USB insertion */
    if (g_charger_ctx.reason == POWERON_REASON_CHARGER) {
        OSI_LOGI(TAG, "USB boot detected - entering fake power-on mode");
        return CHARGER_BOOT_FAKE;
    }
    
    /* Normal boot */
    return CHARGER_BOOT_NORMAL;
}

/**
 * @brief Check if USB/charger is currently connected
 */
bool framework_charger_is_connected(void)
{
    return CommPMVbusDetect();
}

/**
 * @brief Enter fake power-on mode
 */
int framework_charger_fake_poweron(void)
{
    OSI_LOGI(TAG, "=== Entering Fake Power-On Mode ===");
    OSI_LOGI(TAG, "Charging only - monitoring USB and power key");
    
    /* Initialize LED for charging indication */
    framework_led_init();
    framework_led_set_state(LED_STATE_CHARGING);
    
    /* Initialize key detection for exit */
    framework_key_init();
    
    /* Create message queue for key events */
    g_charger_queue = osiMessageQueueCreate(2, 1);
    if (!g_charger_queue) {
        OSI_LOGE(TAG, "Failed to create message queue");
        return -1;
    }
    
    /* Register power key callback */
    framework_key_register_pwk_callback(prv_fake_poweron_key_callback, NULL);
    
    /* Reset exit flag */
    g_exit_fake_mode = false;
    
    /* Create monitoring task */
    osiThread_t *task = osiThreadCreate("fakeChg", prv_fake_poweron_task, 
                                        NULL, 120, 1024);
    if (!task) {
        OSI_LOGE(TAG, "Failed to create fake power-on task");
        osiMessageQueueDelete(g_charger_queue);
        g_charger_queue = NULL;
        return -1;
    }
    
    /* Wait for task to complete */
    while (!g_exit_fake_mode) {
        osiThreadSleep(50);  /* 1 second */
    }
    
    /* Cleanup */
    framework_key_register_pwk_callback(NULL, NULL);
    osiMessageQueueDelete(g_charger_queue);
    g_charger_queue = NULL;
    
    /* Check USB state */
    if (!CommPMVbusDetect()) {
        OSI_LOGI(TAG, "USB disconnected - system will power off");
        return -1;  /* Power off */
    }
    
    OSI_LOGI(TAG, "=== Exiting Fake Power-On Mode ===");
    OSI_LOGI(TAG, "Continuing to normal boot...");
    return 0;  /* Continue to normal boot */
}
