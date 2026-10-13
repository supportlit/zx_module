/**
 * @file framework_msg.c
 * @brief Framework Message Management Module Implementation
 * @details Implements message queue management, message dispatching and default processing logic
 */

#include "framework_msg.h"
#include "framework_log.h"
#include "framework_audio.h"
#include "framework_config.h"
#include "framework_led.h"
#include "framework_key.h"
#include "framework_charger.h"
#include "framework_device_config.h"
#include "osi_api.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define TAG "FRAMEWORK_MSG"

#define MSG_TASK_PRIORITY 100   /**< Message processing task priority */
#define MSG_TASK_STACK_SIZE (10*1024) /**< Message processing task stack size */

/**
 * @brief Message Management Context Structure
 */
typedef struct {
    osiMessageQueue_t *msg_queue;  /**< Message queue handle */
    osiThread_t *msg_task;          /**< Message processing task handle */
    msg_process_cb_t callback;      /**< Customer callback function */
} msg_context_t;

static msg_context_t g_msg_ctx = {0};

/* Forward declarations */
static void prv_msg_task(void *param);
static void prv_handle_key_msg(framework_msg_t *msg);
static void prv_handle_charge_msg(framework_msg_t *msg);
static void prv_handle_network_msg(framework_msg_t *msg);
static void prv_delay_poweroff_callback(void *param);
static void prv_poweroff_wait_release_callback(powkey_event_e event, void *param);

static volatile bool g_poweroff_key_released = false;

static void prv_audio_playback_callback(audio_resource_id_t res_id, audio_playback_status_t status, int ret)
{
    if(res_id == AUDIO_RES_SYS_SHUTDOWN && (status == AUDIO_PLAYBACK_STATUS_ENDED || status == AUDIO_PLAYBACK_STATUS_ERROR )){
        #if FRAMEWORK_POWEROFF_CHARGER_ENABLE
        if(framework_charger_is_connected()){
            OSI_LOGI(TAG, "Power off due to charger connected, perform reset");
            framework_device_set_poweroff_flag(FRAMEWORK_POWER_OFF_FLAG_CHARGER);
            framework_key_register_pwk_callback(NULL, NULL);
            osiSysReset();
            while (1)
            {
                osiThreadSleep(100);
            }
        }
        #endif
        OSI_LOGI(TAG, "Power key long press shutdown - powering off");  
        /* Stop audio, turn off indicators, wait for power key release, then power off */
        audio_resource_stop();
        #ifdef FRAMEWORK_ENABLE_LED
        framework_led_all_off();
        #endif
        framework_key_register_pwk_callback(prv_poweroff_wait_release_callback, NULL);
        while (g_poweroff_key_released) {
            osiThreadSleep(100);
        }
        framework_key_register_pwk_callback(NULL, NULL);
        framework_device_clear_poweroff_flag();
        osiSysPoweroff();
        while (1)
        {
            osiThreadSleep(100);
        }
    }
}

OSI_UNUSED static void prv_poweroff_wait_release_callback(powkey_event_e event, void *param)
{
    (void)param;
    if (event == POWKEY_EVENT_RELEASE) {
        g_poweroff_key_released = false;
    }
}

/**
 * @brief Delayed power-off timer callback
 * @details Schedule work to perform power off
 */
static void prv_delay_poweroff_callback(void *param)
{
    g_poweroff_key_released = true;
    audio_resource_play_with_priority(AUDIO_RES_SYS_SHUTDOWN, prv_audio_playback_callback, AUDIO_PLAY_PIRORITY_URGENT);
    // system will power off in the audio playback callback
}

/**
 * @brief Handle key messages
 * @param msg Message pointer
 */
static void prv_handle_key_msg(framework_msg_t *msg)
{
    uint8_t vol = 0;

    OSI_LOGI(TAG, "Handle key msg: id=0x%lx, state=%s", msg->id, 
             msg->param1 == KEY_STATE_DOWN ? "DOWN" : 
             (msg->param1 == KEY_STATE_LONG_PRESS ? "LONG_PRESS" : "UP"));

    switch (msg->id) {
    case MSG_KEY_VOL_UP:
        if(msg->param1 != KEY_STATE_DOWN)
            break;
        OSI_LOGI(TAG, "Volume up pressed");
        // Call audio manager to increase volume
        if (audio_volume_get() < VOL_MAX)
        {
            vol = audio_volume_get() + VOL_STEP;
            audio_volume_set(vol);
            framework_device_config_set_volume(vol);
            audio_resource_play_mid_priority(AUDIO_RES_VOL_UP, NULL);
        } else {
            audio_resource_play_mid_priority(AUDIO_RES_VOL_MAX, NULL);
        }
        break;
    case MSG_KEY_VOL_DOWN:
        if(msg->param1 != KEY_STATE_DOWN)
            break;
        OSI_LOGI(TAG, "Volume down pressed");
        // Call audio manager to decrease volume
        if (audio_volume_get() > VOL_MIN)
        {
            vol = audio_volume_get() - VOL_STEP;
            audio_volume_set(vol);
            framework_device_config_set_volume(vol);
            audio_resource_play_mid_priority(AUDIO_RES_VOL_DOWN, NULL);
        } else {
            audio_resource_play_mid_priority(AUDIO_RES_VOL_MIN, NULL);
        }
        break;
    case MSG_KEY_POWER:
        if(msg->param1 == KEY_STATE_LONG_PRESS){
            OSI_LOGI(TAG, "Power key long pressed, scheduling power off");
            prv_delay_poweroff_callback(NULL);
            break;
        } else if(msg->param1 == KEY_STATE_UP){
            OSI_LOGI(TAG, "Power key released");
            g_poweroff_key_released = false;
        }
        break;
    case MSG_KEY_FUNCTION:
        OSI_LOGI(TAG, "Function key pressed");
        // Function key handling - can switch play mode etc.
        break;
    default:
        OSI_LOGW(TAG, "Unknown key message id: 0x%lx", msg->id);
        break;
    }
}
/**
 * @brief Handle charge messages
 * @param msg Message pointer
 */
static void prv_handle_charge_msg(framework_msg_t *msg)
{
    OSI_LOGI(TAG, "Handle charge msg: id=0x%lx, param1=%ld, param2=%ld",
             msg->id, msg->param1, msg->param2);

    switch (msg->id) {
    case MSG_CHARGE_PLUGIN:
        OSI_LOGI(TAG, "Charger plugin detected");
        #ifdef FRAMEWORK_ENABLE_LED
        framework_led_clear_state(LED_STATE_EXCEPTION_POWER|LED_STATE_CHARGE_FULL);
        framework_led_set_state(LED_STATE_CHARGING);
        #endif
        audio_resource_play(AUDIO_RES_CHG_CONNECTED, NULL);
        
        break;

    case MSG_CHARGE_PLUGOUT:
        OSI_LOGI(TAG, "Charger plugout detected");
        #ifdef FRAMEWORK_ENABLE_LED
        framework_led_clear_state(LED_STATE_CHARGING|LED_STATE_CHARGE_FULL);
        audio_resource_play(AUDIO_RES_CHG_DISCONNECTED, NULL);
        #endif
        break;

    case MSG_CHARGE_FULL:
        OSI_LOGI(TAG, "Charge full detected");
        #ifdef FRAMEWORK_ENABLE_LED
        framework_led_clear_state(LED_STATE_CHARGING);
        framework_led_set_state(LED_STATE_CHARGE_FULL);
        #endif
        audio_resource_play(AUDIO_RES_BAT_CHARGE_COMPLETE, NULL);
        break;

    case MSG_CHARGE_LOW_POWER:
        OSI_LOGI(TAG, "Low power detected, will shutdown after 5s");
        // Set LED to low battery state
        #ifdef FRAMEWORK_ENABLE_LED
        framework_led_set_state(LED_STATE_EXCEPTION_POWER);
        #endif
        audio_resource_play(AUDIO_RES_BAT_LOW, NULL);
        osiThreadSleep(1000);
        if(framework_charger_is_connected()){
            OSI_LOGI(TAG, "Charger connected during low power warning, aborting shutdown");
            break;
        }
        prv_delay_poweroff_callback(NULL);
        break;

    default:
        OSI_LOGW(TAG, "Unknown charge message id: 0x%lx", msg->id);
        break;
    }
}

/**
 * @brief Handle network messages
 * @param msg Message pointer
 */
static void prv_handle_network_msg(framework_msg_t *msg)
{
    OSI_LOGI(TAG, "Handle network msg: id=0x%lx, param1=%ld, param2=%ld",
             msg->id, msg->param1, msg->param2);

    switch (msg->id) {
    case MSG_NET_CONNECTED:
        OSI_LOGI(TAG, "Network connected");
        // Network connection success handling
        break;

    case MSG_NET_DISCONNECTED:
        OSI_LOGI(TAG, "Network disconnected");
        // Network disconnection handling
        break;

    case MSG_NET_GOT_IP:
        OSI_LOGI(TAG, "Got IP address");
        // Got IP address handling
        break;

    default:
        OSI_LOGW(TAG, "Unknown network message id: 0x%lx", msg->id);
        break;
    }
}

static void prv_handle_mqtt_msg(framework_msg_t *msg)
{
    OSI_LOGI(TAG, "Handle MQTT msg: id=0x%lx, param1=%ld, param2=%ld",
             msg->id, msg->param1, msg->param2);

    switch (msg->id) {
    case MSG_MQTT_CONNECTED:
        OSI_LOGI(TAG, "MQTT connected");
        #ifdef FRAMEWORK_ENABLE_LED
        framework_led_clear_state(LED_STATE_EXCEPTION_NET|LED_STATE_NET_CONNECTING);
        framework_led_set_state(LED_STATE_NET_CONNECTED);
        #endif
        audio_resource_play(AUDIO_RES_NET_CONNECTED, NULL);
        break;

    case MSG_MQTT_DISCONNECTED:
        OSI_LOGI(TAG, "MQTT disconnected");
        #ifdef FRAMEWORK_ENABLE_LED
        framework_led_set_state(LED_STATE_EXCEPTION_NET);
        #endif
        audio_resource_play(AUDIO_RES_SYS_ERROR, NULL);
        // MQTT disconnection handling
        break;
    default:
        OSI_LOGW(TAG, "Unknown MQTT message id: 0x%lx", msg->id);
        break;
    }
}

static void prv_handle_http_msg(framework_msg_t *msg)
{
    OSI_LOGI(TAG, "Handle HTTP msg: id=0x%lx, param1=%ld, param2=%ld",
             msg->id, msg->param1, msg->param2);

    switch (msg->id) {
    case MSG_HTTP_DOWNLOAD_SUCCESS:
        OSI_LOGI(TAG, "HTTP download completed");
        // HTTP download completion handling
        break;

    default:
        OSI_LOGW(TAG, "Unknown HTTP message id: 0x%lx", msg->id);
        break;
    }
}

static void prv_handle_unzip_msg(framework_msg_t *msg)
{
    OSI_LOGI(TAG, "Handle UNZIP msg: id=0x%lx, param1=%ld, param2=%ld",
             msg->id, msg->param1, msg->param2);

    switch (msg->id) {
    case MSG_UNZIP_START:
        OSI_LOGI(TAG, "Unzip task started");
        break;
    case MSG_UNZIP_PROGRESS:
        OSI_LOGI(TAG, "Unzip progress %ld%%", msg->param2);
        break;
    case MSG_UNZIP_SUCCESS:
        OSI_LOGI(TAG, "Unzip task completed successfully");
        break;
    case MSG_UNZIP_FAIL:
        OSI_LOGW(TAG, "Unzip task failed, err=%ld", msg->param2);
        break;
    default:
        OSI_LOGW(TAG, "Unknown unzip message id: 0x%lx", msg->id);
        break;
    }
}

/**
 * @brief Message processing task
 * @param param Task parameter
 */
static void prv_msg_task(void *param)
{
    framework_msg_t msg;
    
    OSI_LOGI(TAG, "Message task started");

    while (1) {
        // Block waiting for message
        if (osiMessageQueueGet(g_msg_ctx.msg_queue, &msg)) {
            OSI_LOGI(TAG, "Received msg: id=0x%lx, param1=%ld, param2=%ld",
                     msg.id, msg.param1, msg.param2);

            // Call customer callback first
            int ret = -1;
            if (g_msg_ctx.callback) {
                ret = g_msg_ctx.callback(&msg);
            }

            if(g_poweroff_key_released && msg.id != MSG_KEY_POWER){
                OSI_LOGI(TAG, "Power off in progress, skip msg: 0x%lx", msg.id);
                continue;
            }

            // If callback returns 0, message is handled, skip default processing
            if (ret == 0) {
                OSI_LOGI(TAG, "Message handled by callback: 0x%lx", msg.id);
                continue;
            }

            // Use default processing logic
            if (MSG_IS_KEY_GROUP(msg.id)) {
                // Key messages
                prv_handle_key_msg(&msg);
            } else if (MSG_IS_CHARGE_GROUP(msg.id)) {
                // Charge messages
                prv_handle_charge_msg(&msg);
            } else if (MSG_IS_NET_GROUP(msg.id)) {
                // Network messages
                prv_handle_network_msg(&msg);
            } else if (MSG_IS_MQTT_GROUP(msg.id)) {
                // MQTT messages
                prv_handle_mqtt_msg(&msg);
            } else if (MSG_IS_HTTP_GROUP(msg.id)) {
                // HTTP messages
                prv_handle_http_msg(&msg);
            } else if (MSG_IS_UNZIP_GROUP(msg.id)) {
                // Unzip messages
                prv_handle_unzip_msg(&msg);
            } else {
                OSI_LOGW(TAG, "Unknown message id: 0x%lx", msg.id);
            }
        } else {
            assert(0);
            OSI_LOGE(TAG, "Failed to get message from queue");
        }
    }
}

/**
 * @brief Initialize message management module
 */
int framework_msg_init(void)
{
    OSI_LOGI(TAG, "Initialize message module");

    if (g_msg_ctx.msg_queue != NULL) {
        OSI_LOGW(TAG, "Message module already initialized");
        return 0;
    }

    // Create message queue
    g_msg_ctx.msg_queue = osiMessageQueueCreate(MSG_QUEUE_SIZE, 
                                                 sizeof(framework_msg_t));
    if (g_msg_ctx.msg_queue == NULL) {
        OSI_LOGE(TAG, "Failed to create message queue");
        return -1;
    }

    // Create message processing task
    g_msg_ctx.msg_task = osiThreadCreate("msg_task",
                                          prv_msg_task,
                                          NULL,
                                          MSG_TASK_PRIORITY,
                                          MSG_TASK_STACK_SIZE);
    if (g_msg_ctx.msg_task == NULL) {
        OSI_LOGE(TAG, "Failed to create message task");
        osiMessageQueueDelete(g_msg_ctx.msg_queue);
        g_msg_ctx.msg_queue = NULL;
        return -1;
    }

    g_msg_ctx.callback = NULL;
    // g_msg_ctx.delay_poweroff_timer = NULL;

    OSI_LOGI(TAG, "Message module initialized successfully");
    return 0;
}

/**
 * @brief Send message
 */
int framework_msg_send(framework_msg_t *msg)
{
    if (msg == NULL) {
        OSI_LOGE(TAG, "Invalid message pointer");
        return -1;
    }

    if (g_msg_ctx.msg_queue == NULL) {
        OSI_LOGE(TAG, "Message queue not initialized");
        return -1;
    }

    OSI_LOGD(TAG, "Sending message: id=0x%lx, param1=%ld, param2=%ld",
             msg->id, msg->param1, msg->param2);

    if (!osiMessageQueueTryPut(g_msg_ctx.msg_queue, msg, 0)) {
        OSI_LOGE(TAG, "Failed to send message, queue full?");
        return -1;
    }

    return 0;
}

/**
 * @brief Register message processing callback function
 */
int framework_msg_register_callback(msg_process_cb_t callback)
{
    if (callback == NULL) {
        OSI_LOGE(TAG, "Invalid callback pointer");
        return -1;
    }

    g_msg_ctx.callback = callback;
    OSI_LOGI(TAG, "Message callback registered");
    
    return 0;
}
