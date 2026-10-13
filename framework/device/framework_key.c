/**
 * @file framework_key.c
 * @brief Framework Key Management Module Implementation
 * @details Implements GPIO key detection, debouncing, long press detection and Power Key handling
 */

#include "framework_key.h"
#include "framework_msg.h"
#include "framework_log.h"
#include "framework_config.h"
#include "osi_api.h"
#include "gpio_api.h"
#include <stdlib.h>
#include <string.h>
#include "drv_keypad.h"
#include "drv_iomux.h"

#define TAG "FRAMEWORK_KEY"

#define ANTI_SHAKE_MS           60    /**< Anti-shake delay (ms) */
#define LONG_PRESS_TIMEOUT_MS   2000  /**< Default long press timeout (ms) */
#define POWER_KEY_LONGPRESS_MS  3000  /**< Power Key long press shutdown timeout (ms) */
#define VBUS_FILTER_TIME_MS     80    /**< VBUS filter time (ms) */
#define KEY_ANTISHAKE_QUEUE_LEN 50    /**< Anti-shake queue length */

/**
 * @brief Key GPIO Configuration
 */
typedef struct {
    uint32_t gpio_id;      /**< GPIO ID */
    uint8_t active_low;    /**< 1=active low */
    uint8_t enabled;       /**< Enable flag */
} key_gpio_config_t;

/**
 * @brief Key State Structure
 */
typedef struct {
    uint32_t id;                    /**< GPIO PIN ID */
    uint32_t cur_state;             /**< Current state */
    osiTimer_t *long_press_timer;   /**< Long press timer */
    osiWork_t *work;                /**< Work queue */
    GPIOCallback cb;                /**< GPIO interrupt callback */
} key_state_t;

/**
 * @brief Power Key Context
 */
typedef struct {
    power_key_callback_t callback;  /**< Callback function */
    void *param;                     /**< User parameter */
    osiTimer_t *long_press_timer;   /**< Long press timer */
    osiTimer_t *vbus_delay_timer;   /**< VBUS delay check timer (60ms) */
    uint32_t press_time;             /**< Press timestamp (ticks) */
    uint16_t long_press_time_ms;    /**< Long press time configuration */
    uint8_t last_vbus_state;        /**< Last VBUS state */
} power_key_context_t;

static key_state_t g_keys[3] = {0};
static power_key_context_t g_powkey_ctx = {0};
static osiMessageQueue_t *g_anti_shake_queue = NULL;
static osiThread_t *g_anti_shake_thread = NULL;
static uint16_t g_key_longpress_time_ms = LONG_PRESS_TIMEOUT_MS;

/* Forward declarations */
static void prvKeyUpISR(void);
static void prvKeyDownISR(void);
static void prvKeyFunISR(void);
static void prv_anti_shake_handle(void *ctx);
static void prv_anti_shake_task(void *param);
static void prv_long_press_timer_callback(void *ctx);
static void prv_long_press_detect_work(void *data);
static void prv_power_key_handler(void);
static void prv_power_key_longpress_callback(void *param);
static void prv_vbus_delay_check_callback(void *param);
static void prv_vbus_delay_check_work(void *param);
static uint32_t prv_key_int_init(uint32_t port, GPIOCallback cb);
static key_state_t *prv_get_key_by_id(uint32_t id);
static int prv_get_key_msg_by_id(uint32_t id);
static void long_press_timer_start(void *data);

extern uint8_t CommPMVbusDetect(void);

/**
 * @brief Get key state structure
 */
static key_state_t *prv_get_key_by_id(uint32_t id)
{
    for (int i = 0; i < 3; i++) {
        if (g_keys[i].id == id)
            return &g_keys[i];
    }
    return NULL;
}

/**
 * @brief Get message ID by GPIO ID
 */
static int prv_get_key_msg_by_id(uint32_t id)
{
    switch (id) {
    case KEY_FUN_PIN:
        return MSG_KEY_FUNCTION;
    case KEY_DOWN_PIN:
        return MSG_KEY_VOL_DOWN;
    case KEY_UP_PIN:
        return MSG_KEY_VOL_UP;
    default:
        break;
    }
    return -1;
}

static void prv_diable_key_interrupts(uint32_t pin)
{
    GpioDisableEdgeDetection(pin, GPIO_TWO_EDGE);
    // GpioDisableEdgeDetection(KEY_DOWN_PIN, GPIO_TWO_EDGE);
    // GpioDisableEdgeDetection(KEY_FUN_PIN, GPIO_TWO_EDGE);
}

static void prv_enable_key_interrupts(uint32_t pin)
{
    GpioEnableEdgeDetection(pin, GPIO_TWO_EDGE);
    // GpioEnableEdgeDetection(KEY_DOWN_PIN, GPIO_TWO_EDGE);
    // GpioEnableEdgeDetection(KEY_FUN_PIN, GPIO_TWO_EDGE);
}

/**
 * @brief GPIO Interrupt Handler - UP Key
 */
static void prvKeyUpISR(void)
{
    uint8_t msg[2] = {KEY_UP_PIN, 0};
    key_state_t *cstate = prv_get_key_by_id(KEY_UP_PIN);
    uint8_t cur_val = GpioGetLevel(KEY_UP_PIN);
    if((cur_val == 0  && cstate->cur_state != KEY_STATE_UP)) {
        return;
    }
    if((cur_val == 1  && cstate->cur_state == KEY_STATE_UP)) {
        return;
    }
    // GpioDisableEdgeDetection(KEY_UP_PIN, GPIO_TWO_EDGE);
    prv_diable_key_interrupts(KEY_UP_PIN);
    osiMessageQueueTryPut(g_anti_shake_queue, msg, 0);
}

/**
 * @brief GPIO Interrupt Handler - DOWN Key
 */
static void prvKeyDownISR(void)
{
    uint8_t msg[2] = {KEY_DOWN_PIN, 0};
    key_state_t *cstate = prv_get_key_by_id(KEY_DOWN_PIN);
    uint8_t cur_val = GpioGetLevel(KEY_DOWN_PIN);
    if((cur_val == 0  && cstate->cur_state != KEY_STATE_UP)) {
        return;
    }
    if((cur_val == 1  && cstate->cur_state == KEY_STATE_UP)) {
        return;
    }
    // GpioDisableEdgeDetection(KEY_DOWN_PIN, GPIO_TWO_EDGE);
    prv_diable_key_interrupts(KEY_DOWN_PIN);
    osiMessageQueueTryPut(g_anti_shake_queue, msg, 0);
}

/**
 * @brief GPIO Interrupt Handler - FUN Key
 */
static void prvKeyFunISR(void)
{
    uint8_t msg[2] = {KEY_FUN_PIN, 0};
    key_state_t *cstate = prv_get_key_by_id(KEY_FUN_PIN);
    uint8_t cur_val = GpioGetLevel(KEY_FUN_PIN);
    if((cur_val == 0  && cstate->cur_state != KEY_STATE_UP)) {
        return;
    }
    if((cur_val == 1  && cstate->cur_state == KEY_STATE_UP)) {
        return;
    }
    // GpioDisableEdgeDetection(KEY_FUN_PIN, GPIO_TWO_EDGE);
    prv_diable_key_interrupts(KEY_FUN_PIN);
    osiMessageQueueTryPut(g_anti_shake_queue, msg, 0);
}

/**
 * @brief Long press detection work
 */
static void prv_long_press_detect_work(void *data)
{
    int key = (int)data;
    key_state_t *cstate = NULL;

    cstate = prv_get_key_by_id(key);
    if (!cstate) {
        return;
    }

    // Start long press detection timer
    if (cstate->long_press_timer == NULL) {
        cstate->long_press_timer = osiTimerCreate(prv_long_press_timer_callback, cstate);
        osiTimerStart(cstate->long_press_timer, g_key_longpress_time_ms);
    }
    
    osiWorkDelete(cstate->work);
    cstate->work = NULL;
}

/**
 * @brief Start long press timer
 */
static void long_press_timer_start(void *data)
{
    uint32_t key = (uint32_t)data;
    key_state_t *cstate = NULL;
    
    cstate = prv_get_key_by_id(key);
    if (!cstate) {
        return;
    }
    
    cstate->work = osiWorkCreate((osiCallback_t)prv_long_press_detect_work, NULL, (void *)data);
    osiWorkEnqueue(cstate->work, osiSysWorkQueueHighPriority());
}

/**
 * @brief Anti-shake timer callback
 */
static void prv_anti_shake_handle(void *ctx)
{
    int pin_id = (int)ctx;
    framework_msg_t msg = {0};
    key_state_t *cstate = NULL;
    prv_enable_key_interrupts(pin_id);
    cstate = prv_get_key_by_id(pin_id);
    if (!cstate) {
        return;
    }

    msg.id = prv_get_key_msg_by_id(pin_id);
    /* Detect current key state */
    if (GpioGetLevel(pin_id) == 0) {
        if (cstate->cur_state == KEY_STATE_UP) {
            cstate->cur_state = KEY_STATE_DOWN;
            msg.param1 = KEY_STATE_DOWN;
            msg.param2 = osiGetTicks();
            msg.param3 = pin_id;
            framework_msg_send(&msg);
            
            /* Start long press detection */
            if (cstate->long_press_timer == NULL && cstate->work == NULL) {
                long_press_timer_start((void *)pin_id);
            }
        }
    } else {
        if (cstate->cur_state == KEY_STATE_DOWN || cstate->cur_state == KEY_STATE_LONG_PRESS) {
            /* Stop long press timer */
            if (cstate->long_press_timer) {
                osiTimerStop(cstate->long_press_timer);
                osiTimerDelete(cstate->long_press_timer);
                cstate->long_press_timer = NULL;
            }
            
            msg.param1 = KEY_STATE_UP;
            msg.param2 = osiGetTicks();
            msg.param3 = pin_id;
            framework_msg_send(&msg);
            cstate->cur_state = KEY_STATE_UP;
        }
    }
}

/**
 * @brief Anti-shake task thread
 */
static void prv_anti_shake_task(void *param)
{
    uint8_t recv_msg[2] = {0};
    int pin_id = 0;

    OSI_LOGI(TAG, "Anti-shake task started");

    while (1) {
        if (!osiMessageQueueGet(g_anti_shake_queue, recv_msg)) {
            continue;
        }

        pin_id = recv_msg[0];
        if(GpioGetLevel(pin_id) == 0){
            osiThreadSleepMs(ANTI_SHAKE_MS);
        } else {
            osiThreadSleepMs(10);
        }
        prv_anti_shake_handle((void *)pin_id);
    }
}

/**
 * @brief Long press timer callback
 */
static void prv_long_press_timer_callback(void *ctx)
{
    framework_msg_t msg = {0};
    if (ctx == NULL) {
        return;
    }
    
    key_state_t *kstate = (key_state_t *)ctx;

    osiTimerStop(kstate->long_press_timer);
    osiTimerDelete(kstate->long_press_timer);
    kstate->long_press_timer = NULL;

    if (GpioGetLevel(kstate->id) == 1) {
        return;
    }

    if (kstate->cur_state == KEY_STATE_DOWN) {
        kstate->cur_state = KEY_STATE_LONG_PRESS;
    }

    msg.id = prv_get_key_msg_by_id(kstate->id);
    msg.param1 = KEY_STATE_LONG_PRESS;
    msg.param2 = osiGetTicks();
    msg.param3 = kstate->id;
    framework_msg_send(&msg);
}

static PowerKeyType_e prvGetPowerKeyType(void)
{
    return PWRKEY_EXTON1;
}

/**
 * @brief VBUS delay check work function
 * @details Executed in work queue to send message and start long press timer
 */
static void prv_vbus_delay_check_work(void *param)
{
    framework_msg_t msg = {0};
    pwrState_t keyStatus = PWRKEY_STATE_ERROR;
    
    /* Recheck power key status after 60ms */
    keyStatus = drvPwrKeyGetStatus(prvGetPowerKeyType());
    
    if (keyStatus == PWRKEY_STATE_PRESS) {
        /* Key is still pressed after 60ms, send press message */
        msg.id = MSG_KEY_POWER;
        msg.param1 = KEY_STATE_DOWN;
        msg.param2 = osiGetTicks();
        framework_msg_send(&msg);
        
        OSI_LOGI(TAG, "Power key pressed (after VBUS delay check)");
        
        /* Now start long press detection timer (3s) */
        if (g_powkey_ctx.long_press_timer == NULL) {
            g_powkey_ctx.long_press_timer = osiTimerCreate(prv_power_key_longpress_callback, NULL);
            osiTimerStart(g_powkey_ctx.long_press_timer, g_powkey_ctx.long_press_time_ms);
        }
    } else {
        /* Key was released within 60ms, this was likely a VBUS glitch */
        OSI_LOGI(TAG, "Power key press filtered (VBUS glitch detected)");
    }
}

/**
 * @brief VBUS delay check timer callback (60ms)
 * @details After 60ms delay, schedule work to check power key status
 */
static void prv_vbus_delay_check_callback(void *param)
{
    osiWork_t *work = NULL;
    
    /* Stop and delete VBUS delay timer */
    if (g_powkey_ctx.vbus_delay_timer) {
        osiTimerStop(g_powkey_ctx.vbus_delay_timer);
        osiTimerDelete(g_powkey_ctx.vbus_delay_timer);
        g_powkey_ctx.vbus_delay_timer = NULL;
    }
    
    /* Schedule work to handle the delay check */
    work = osiWorkCreate((osiCallback_t)prv_vbus_delay_check_work, NULL, NULL);
    if (work) {
        osiWorkEnqueue(work, osiSysWorkQueueHighPriority());
    }
}

/**
 * @brief Power Key event handler
 */
static void prv_power_key_handler(void)
{
    pwrState_t keyStatus = PWRKEY_STATE_ERROR;
    static uint32_t press_start_time = 0;
    framework_msg_t msg = {0};

    keyStatus = drvPwrKeyGetStatus(prvGetPowerKeyType());
    
    if (keyStatus == PWRKEY_STATE_PRESS) {
        press_start_time = osiGetTicks();
        
        /* Stop and delete old timers */
        if (g_powkey_ctx.long_press_timer != NULL) {
            osiTimerStop(g_powkey_ctx.long_press_timer);
            osiTimerDelete(g_powkey_ctx.long_press_timer);
            g_powkey_ctx.long_press_timer = NULL;
        }
        if (g_powkey_ctx.vbus_delay_timer != NULL) {
            osiTimerStop(g_powkey_ctx.vbus_delay_timer);
            osiTimerDelete(g_powkey_ctx.vbus_delay_timer);
            g_powkey_ctx.vbus_delay_timer = NULL;
        }
        
        /* Check if VBUS is present */
        if (CommPMVbusDetect()) {
            /* VBUS is present, start 60ms delay timer to filter glitches */
            g_powkey_ctx.vbus_delay_timer = osiTimerCreate(prv_vbus_delay_check_callback, NULL);
            osiTimerStart(g_powkey_ctx.vbus_delay_timer, 80);
            OSI_LOGI(TAG, "Power key press detected with VBUS, starting 80ms delay check");
        } else {
            /* VBUS not present, directly send press message and start long press timer */
            msg.id = MSG_KEY_POWER;
            msg.param1 = KEY_STATE_DOWN;
            msg.param2 = osiGetTicks();
            framework_msg_send(&msg);
            
            OSI_LOGI(TAG, "Power key pressed (no VBUS)");
            
            /* Start long press detection timer */
            g_powkey_ctx.long_press_timer = osiTimerCreate(prv_power_key_longpress_callback, NULL);
            osiTimerStart(g_powkey_ctx.long_press_timer, g_powkey_ctx.long_press_time_ms);
        }
    } 
    else if (keyStatus == PWRKEY_STATE_RELEASE) {
        uint32_t press_duration = osiGetTicks() - press_start_time;
        
        /* Stop VBUS delay timer if active */
        if (g_powkey_ctx.vbus_delay_timer) {
            osiTimerStop(g_powkey_ctx.vbus_delay_timer);
            osiTimerDelete(g_powkey_ctx.vbus_delay_timer);
            g_powkey_ctx.vbus_delay_timer = NULL;
            /* Key was released during VBUS delay check period, ignore this press */
            OSI_LOGI(TAG, "Power key released during VBUS delay check, press ignored");
            return;
        }
        
        /* Stop long press timer */
        if (g_powkey_ctx.long_press_timer) {
            osiTimerStop(g_powkey_ctx.long_press_timer);
            osiTimerDelete(g_powkey_ctx.long_press_timer);
            g_powkey_ctx.long_press_timer = NULL;
            
            /* Filter Power Key brief high caused by VBUS */
            if ((!g_powkey_ctx.last_vbus_state) || (press_duration < VBUS_FILTER_TIME_MS/5)) {
                if (CommPMVbusDetect()) {
                    g_powkey_ctx.last_vbus_state = 1;
                    return;
                }
            }

            msg.id = MSG_KEY_POWER;
            msg.param1 = KEY_STATE_UP;
            msg.param2 = press_duration * 5;
            framework_msg_send(&msg);
            
            /* Short press event */
            if (g_powkey_ctx.callback) {
                g_powkey_ctx.callback(POWKEY_EVENT_PRESS, g_powkey_ctx.param);
            }
        } else {
            if (press_duration < VBUS_FILTER_TIME_MS) {
                return;
            }
            msg.id = MSG_KEY_POWER;
            msg.param1 = KEY_STATE_UP;
            msg.param2 = press_duration * 5;
            framework_msg_send(&msg);

            /* Release after long press */
            if (g_powkey_ctx.callback) {
                g_powkey_ctx.callback(POWKEY_EVENT_RELEASE, g_powkey_ctx.param);
            }
        }
        
        OSI_LOGI(TAG, "Power key released, duration=%ldms", press_duration * 5);
    }
}

/**
 * @brief Power Key long press timer callback
 */
static void prv_power_key_longpress_callback(void *param)
{
    framework_msg_t msg = {0};

    pwrState_t keyStatus = PWRKEY_STATE_ERROR;
    keyStatus = drvPwrKeyGetStatus(prvGetPowerKeyType());
    if (keyStatus == PWRKEY_STATE_RELEASE) {
        return;
    }

    if (g_powkey_ctx.long_press_timer) {
        osiTimerStop(g_powkey_ctx.long_press_timer);
        osiTimerDelete(g_powkey_ctx.long_press_timer);
        g_powkey_ctx.long_press_timer = NULL;
    }

    OSI_LOGI(TAG, "Power key long pressed, shutdown");

    if (g_powkey_ctx.callback) {
        g_powkey_ctx.callback(POWKEY_EVENT_LONG_PRESS, g_powkey_ctx.param);
    } else {
        msg.id = MSG_KEY_POWER;
        msg.param1 = KEY_STATE_LONG_PRESS;
        msg.param2 = osiGetTicks();
        framework_msg_send(&msg);
    }
}

/**
 * @brief GPIO key initialization
 */
static uint32_t prv_key_int_init(uint32_t port, GPIOCallback cb)
{
    GPIOConfiguration config = {
        .pinDir = GPIO_IN_PIN,
        .initLv = 0,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_TWO_EDGE,
        .isr = cb,
        .wu = NULL
    };
    
    GPIOReturnCode ret = GpioInitConfiguration(port, config);
    GpioSetDirection(port, GPIO_IN_PIN);
    
    if (ret == GPIORC_OK) {
        OSI_LOGI(TAG, "GPIO[%ld] init success", port);
    } else {
        OSI_LOGE(TAG, "GPIO[%ld] init failed", port);
    }
    
    return GpioGetLevel(port);
}

/**
 * @brief GPIO PINMUX configuration
 */
static void prv_gpio_init(void)
{
    unsigned int mfp_key[] = {
        MFP_REG(KEY_DOWN_PINMUX) | KEY_DOWN_FUN | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(KEY_UP_PINMUX)   | KEY_UP_FUN   | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(KEY_FUN_PINMUX)  | KEY_FUN_FUN  | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    drvGpioPinMuxConfig(mfp_key);
}

/**
 * @brief Initialize key management module
 */
int framework_key_init(void)
{
    uint32_t initVal = 0;
    uint32_t val = 0;

    OSI_LOGI(TAG, "Initialize key module");

    if (g_anti_shake_queue != NULL) {
        OSI_LOGW(TAG, "Key module already initialized");
        return 0;
    }

    /* Configure GPIO PINMUX */
    prv_gpio_init();

    /* Initialize key state structure */
    g_keys[0].id = KEY_DOWN_PIN;
    g_keys[0].cb = prvKeyDownISR;
    g_keys[1].id = KEY_UP_PIN;
    g_keys[1].cb = prvKeyUpISR;
    g_keys[2].id = KEY_FUN_PIN;
    g_keys[2].cb = prvKeyFunISR;

    /* Create anti-shake queue */
    g_anti_shake_queue = osiMessageQueueCreate(KEY_ANTISHAKE_QUEUE_LEN, 2);
    if (g_anti_shake_queue == NULL) {
        OSI_LOGE(TAG, "Failed to create anti-shake queue");
        return -1;
    }

    /* Create anti-shake thread */
    g_anti_shake_thread = osiThreadCreate("antiShake", 
                                          prv_anti_shake_task, 
                                          NULL, 
                                          KEY_TASK_PRIORITY, 
                                          2 * 1024);
    if (g_anti_shake_thread == NULL) {
        OSI_LOGE(TAG, "Failed to create anti-shake thread");
        return -1;
    }

    /* Initialize GPIO keys */
    for (int i = 0; i < 3; i++) {
        g_keys[i].cur_state = KEY_STATE_UP;
        val = prv_key_int_init(g_keys[i].id, g_keys[i].cb);
        if (val == 1) {
            initVal |= (0x01 << i);
        }
    }

    /* Initialize Power Key */
    memset(&g_powkey_ctx, 0, sizeof(g_powkey_ctx));
    g_powkey_ctx.long_press_time_ms = POWER_KEY_LONGPRESS_MS;
    g_powkey_ctx.vbus_delay_timer = NULL;
    
    /* Register Power Key callback */
    drvPwrKeySetCB(prvGetPowerKeyType(), prv_power_key_handler);

    /* Initialize VBUS state */
    if (CommPMVbusDetect()) {
        g_powkey_ctx.last_vbus_state = 1;
    }

    OSI_LOGI(TAG, "Key module initialized successfully, initVal=0x%lx", initVal);
    return initVal;
}

/**
 * @brief Enable/disable key detection
 */
int framework_key_enable(key_id_e key_id, uint8_t enable)
{
    uint32_t pin_id = 0;
    
    /* Select GPIO pin based on key_id */
    switch (key_id) {
    case KEY_ID_VOLUME_DOWN:
        pin_id = KEY_DOWN_PIN;
        break;
    case KEY_ID_VOLUME_UP:
        pin_id = KEY_UP_PIN;
        break;
    case KEY_ID_FUNCTION:
        pin_id = KEY_FUN_PIN;
        break;
    default:
        OSI_LOGE(TAG, "Invalid key_id: %d", key_id);
        return -1;
    }
    
    if (enable) {
        GpioEnableEdgeDetection(pin_id, GPIO_TWO_EDGE);
        OSI_LOGI(TAG, "Key %d enabled", key_id);
    } else {
        GpioDisableEdgeDetection(pin_id, GPIO_TWO_EDGE);
        OSI_LOGI(TAG, "Key %d disabled", key_id);
    }
    
    return 0;
}

/**
 * @brief Register Power Key callback function
 */
int framework_key_register_pwk_callback(power_key_callback_t callback, void *param)
{
    g_powkey_ctx.callback = callback;
    g_powkey_ctx.param = param;
    
    OSI_LOGI(TAG, "Power key callback registered");
    return 0;
}

/**
 * @brief Set Power Key long press time
 */
int framework_key_set_pwk_longpress_time(uint16_t ms)
{
    if (ms < 1000 || ms > 10000) {
        OSI_LOGE(TAG, "Invalid longpress time: %d ms", ms);
        return -1;
    }
    
    g_powkey_ctx.long_press_time_ms = ms;
    OSI_LOGI(TAG, "Power key longpress time set to %d ms", ms);
    return 0;
}

int framework_key_set_longpress_time(uint16_t ms)
{
    if (ms < 500 || ms > 10000) {
        OSI_LOGE(TAG, "Invalid normal key longpress time: %d ms", ms);
        return -1;
    }

    g_key_longpress_time_ms = ms;
    OSI_LOGI(TAG, "Normal key longpress time set to %d ms", ms);
    return 0;
}

/**
 * @brief Update VBUS state
 */
void framework_key_update_vbus_state(uint8_t state)
{
    g_powkey_ctx.last_vbus_state = state;
    OSI_LOGD(TAG, "VBUS state updated to %s", state ? "connected" : "disconnected");
}

/**
 * @brief Get power-on reason
 */
uint8_t framework_key_get_poweron_reason(void)
{
    if (osiGetRestartReason() == 'C') {
        if (drvPwrKeyGetStatus(PWRKEY_EXTON1) == PWRKEY_STATE_RELEASE) {
            /* Device powered on by USB */
            return 2;
        } else if (drvPwrKeyGetStatus(PWRKEY_EXTON1) == PWRKEY_STATE_PRESS) {
            /* Device powered on by key press while charging */
            return 1;
        }
    }
    /* Device powered on by key press without charging */
    return 0;
}
