/**
 * @file customer_handlers.c
 * @brief template 客户主处理器 - 初始化与消息处理
 */

#include "customer_handlers.h"
#include "customer_mqtt_handler.h"
#include "customer_payment_handler.h"
#include "customer_status_report.h"
#include "customer_battery_config.h"
#include "app_lcd.h"
#include "framework_device_config.h"
#include "framework_config.h"
#include "customer_mqtt_handler.h" // for CUSTOMER_DEVICE_SN
#include "framework_msg.h"
#include "framework_log.h"
#include "framework_audio.h"
#include "framework_http.h"
#include "framework_unzip.h"
#include "framework_led.h"
#include "framework_battery.h"
#include "framework_mqtt.h"
#include "framework_network.h"
#include "framework_charger.h"
#include "audio_resource_map.h"
#include "fota_api.h"
#include <string.h>
#include <stdint.h>
#include "drv_iomux.h"
#include "drv_def.h"
#include "gpio_api.h"
#include "fs_api.h"
#include "mbedtls/md5.h"
#include "at_api.h"
#include "customer_register.h"
#define TAG "TEMPLATE"

#define LED_W1_PIN 54
#define LED_W2_PIN 84
#define LED_W3_PIN 37
#define LED_W4_PIN 86

#define LED_GPIO_W1 GPIO_54
#define LED_GPIO_W2 CLK_REQ
#define LED_GPIO_W3 GPIO_37
#define LED_GPIO_W4 GPIO_VCXO_OUT

#define LED_FUN_W1 MFP_AF0
#define LED_FUN_W2 MFP_AF1
#define LED_FUN_W3 MFP_AF0
#define LED_FUN_W4 MFP_AF1

/* LED behavior (template)
 * Left ring:
 *  - Fast green blink: SIM not inserted
 *  - Alt blue/green: network attach failed (cellular & WiFi)
 *  - Slow blue blink: on cellular network, server not connected
 *  - Fast blue blink: on WiFi network, server not connected
 *  - Solid blue: server connected (any bearer)
 *  - Rapid alt blue/green: switching WiFi/cellular
 * Battery ring (right) unchanged.
 * Power/battery:
 *  - Solid red: charging
 *  - Red off: full or not charging
 *  - Rapid red blink: low/critical battery
 * Error:
 *  - Alt red/blue: system/firmware error
 */

static char ap_cfg_ssid[128] = {0};

typedef enum
{
    BK_LED_NET_NONE = 0,
    BK_LED_NET_FAIL,
    BK_LED_NET_CELL_WAIT,
    BK_LED_NET_WIFI_WAIT,
    BK_LED_NET_SERVER_OK,
    BK_LED_NET_SWITCHING,
    BK_LED_NET_SIM_MISSING,
} bk_led_net_state_e;

typedef enum
{
    BK_LED_PWR_NONE = 0,
    BK_LED_PWR_CHARGING,
    BK_LED_PWR_FULL,
    BK_LED_PWR_LOW,
} bk_led_pwr_state_e;

typedef enum
{
    BK_LED_ERR_NONE = 0,
    BK_LED_ERR_SYS,
} bk_led_err_state_e;

typedef enum
{
    BK_PATTERN_OFF = 0,
    BK_PATTERN_SOLID_RED,
    BK_PATTERN_BLINK_RED_RAPID,
    BK_PATTERN_ALT_RED_BLUE,
    BK_PATTERN_SOLID_BLUE,
    BK_PATTERN_BLINK_BLUE_SLOW,
    BK_PATTERN_BLINK_BLUE_FAST,
    BK_PATTERN_ALT_BLUE_GREEN,
    BK_PATTERN_ALT_BLUE_GREEN_RAPID,
    BK_PATTERN_BLINK_GREEN_FAST,
} bk_led_pattern_e;

typedef struct
{
    bk_led_pattern_e pattern;
    uint32_t interval_on_ms;
    uint32_t interval_off_ms;
    bool use_alt;
    bool state_on;
} bk_led_runtime_t;

typedef struct
{
    uint8_t net_mode;
    osiTimer_t *function_press_timer;
    osiTimer_t *vol_up_press_timer;
    osiWork_t *network_bearer_work;
    osiTimer_t *abnormal_work_timer;
    osiWork_t *abnormal_work;
    uint8_t abnormal_work_flag;
    osiTimer_t *function_long_press_timer;
    osiWork_t *function_long_press_work;
    uint8_t battery_full;
} template_handlers_t;

static template_handlers_t template_handlers = {0};
static osiTimer_t *s_bk_led_timer = NULL;
static bk_led_net_state_e s_bk_net_state = BK_LED_NET_FAIL;
static bk_led_pwr_state_e s_bk_pwr_state = BK_LED_PWR_NONE;
static bk_led_err_state_e s_bk_err_state = BK_LED_ERR_NONE;
static bk_led_pattern_e s_bk_active_pattern = BK_PATTERN_OFF;
static bool s_bk_alt_toggle = false;
static bk_led_runtime_t s_bk_runtime = {0};

static inline void bk_led_set_rgb(bool r, bool g, bool b)
{
    GpioSetLevel(LED_RED_PIN, r);
    GpioSetLevel(LED_GREEN_PIN, g);
    GpioSetLevel(LED_BLUE_PIN, b);
}

static void bk_led_timer_cb(void *param)
{
    (void)param;
    s_bk_runtime.state_on = !s_bk_runtime.state_on;
    if (s_bk_runtime.use_alt)
    {
        s_bk_alt_toggle = !s_bk_alt_toggle;
    }

    switch (s_bk_active_pattern)
    {
    case BK_PATTERN_BLINK_RED_RAPID:
        bk_led_set_rgb(s_bk_runtime.state_on, false, false);
        break;
    case BK_PATTERN_ALT_RED_BLUE:
        bk_led_set_rgb(s_bk_alt_toggle, false, !s_bk_alt_toggle);
        break;
    case BK_PATTERN_BLINK_BLUE_SLOW:
    case BK_PATTERN_BLINK_BLUE_FAST:
        bk_led_set_rgb(false, false, s_bk_runtime.state_on);
        break;
    case BK_PATTERN_ALT_BLUE_GREEN:
    case BK_PATTERN_ALT_BLUE_GREEN_RAPID:
        bk_led_set_rgb(false, !s_bk_alt_toggle, s_bk_alt_toggle);
        break;
    case BK_PATTERN_BLINK_GREEN_FAST:
        bk_led_set_rgb(false, s_bk_runtime.state_on, false);
        break;
    default:
        break;
    }

    uint32_t next = s_bk_runtime.state_on ? s_bk_runtime.interval_on_ms : s_bk_runtime.interval_off_ms;
    if (next == 0)
    {
        next = 200;
    }
    osiTimerStart(s_bk_led_timer, next);
}

static void bk_led_stop_timer(void)
{
    if (s_bk_led_timer)
    {
        osiTimerStop(s_bk_led_timer);
    }
}

static void bk_led_apply_pattern(bk_led_pattern_e pattern)
{
    if (s_bk_active_pattern == pattern && pattern != BK_PATTERN_OFF)
    {
        return;
    }

    s_bk_active_pattern = pattern;
    s_bk_alt_toggle = false;
    s_bk_runtime.state_on = true;
    s_bk_runtime.use_alt = false;
    s_bk_runtime.interval_on_ms = 500;
    s_bk_runtime.interval_off_ms = 500;

    bk_led_stop_timer();

    switch (pattern)
    {
    case BK_PATTERN_OFF:
        bk_led_set_rgb(false, false, false);
        return;
    case BK_PATTERN_SOLID_RED:
        bk_led_set_rgb(true, false, false);
        return;
    case BK_PATTERN_BLINK_RED_RAPID:
        bk_led_set_rgb(true, false, false);
        s_bk_runtime.interval_on_ms = 200;
        s_bk_runtime.interval_off_ms = 200;
        break;
    case BK_PATTERN_ALT_RED_BLUE:
        bk_led_set_rgb(true, false, false);
        s_bk_runtime.use_alt = true;
        s_bk_runtime.interval_on_ms = 400;
        s_bk_runtime.interval_off_ms = 400;
        break;
    case BK_PATTERN_SOLID_BLUE:
        bk_led_set_rgb(false, false, true);
        return;
    case BK_PATTERN_BLINK_BLUE_SLOW:
        bk_led_set_rgb(false, false, true);
        s_bk_runtime.interval_on_ms = 300;
        s_bk_runtime.interval_off_ms = 1700;
        break;
    case BK_PATTERN_BLINK_BLUE_FAST:
        bk_led_set_rgb(false, false, true);
        s_bk_runtime.interval_on_ms = 300;
        s_bk_runtime.interval_off_ms = 200;
        break;
    case BK_PATTERN_ALT_BLUE_GREEN:
        bk_led_set_rgb(false, true, false);
        s_bk_runtime.use_alt = true;
        s_bk_runtime.interval_on_ms = 700;
        s_bk_runtime.interval_off_ms = 700;
        break;
    case BK_PATTERN_ALT_BLUE_GREEN_RAPID:
        bk_led_set_rgb(false, true, false);
        s_bk_runtime.use_alt = true;
        s_bk_runtime.interval_on_ms = 250;
        s_bk_runtime.interval_off_ms = 250;
        break;
    case BK_PATTERN_BLINK_GREEN_FAST:
        bk_led_set_rgb(false, true, false);
        s_bk_runtime.interval_on_ms = 300;
        s_bk_runtime.interval_off_ms = 200;
        break;
    default:
        return;
    }

    if (s_bk_led_timer == NULL)
    {
        s_bk_led_timer = osiTimerCreate(bk_led_timer_cb, NULL);
    }
    osiTimerStart(s_bk_led_timer, s_bk_runtime.interval_on_ms);
}

static void bk_led_update(void)
{
    if (s_bk_err_state == BK_LED_ERR_SYS)
    {
        bk_led_apply_pattern(BK_PATTERN_ALT_RED_BLUE);
        return;
    }

    /* Network states outrank power: show any non-OK network condition first. */
    if (s_bk_net_state != BK_LED_NET_SERVER_OK)
    {
        switch (s_bk_net_state)
        {
        case BK_LED_NET_SIM_MISSING:
            bk_led_apply_pattern(BK_PATTERN_BLINK_GREEN_FAST);
            break;
        case BK_LED_NET_FAIL:
            bk_led_apply_pattern(BK_PATTERN_ALT_BLUE_GREEN);
            break;
        case BK_LED_NET_SWITCHING:
            bk_led_apply_pattern(BK_PATTERN_ALT_BLUE_GREEN_RAPID);
            break;
        case BK_LED_NET_CELL_WAIT:
            bk_led_apply_pattern(BK_PATTERN_BLINK_BLUE_SLOW);
            break;
        case BK_LED_NET_WIFI_WAIT:
            bk_led_apply_pattern(BK_PATTERN_BLINK_BLUE_FAST);
            break;
        default:
            bk_led_apply_pattern(BK_PATTERN_OFF);
            break;
        }
        return;
    }

    if (s_bk_pwr_state == BK_LED_PWR_LOW)
    {
        bk_led_apply_pattern(BK_PATTERN_BLINK_RED_RAPID);
        return;
    }

    if (s_bk_pwr_state == BK_LED_PWR_CHARGING)
    {
        bk_led_apply_pattern(BK_PATTERN_SOLID_RED);
        return;
    }

    if (s_bk_pwr_state == BK_LED_PWR_FULL)
    {
        bk_led_apply_pattern(BK_PATTERN_SOLID_RED);
        return;
    }

    /* Default: network OK and idle. */
    bk_led_apply_pattern(BK_PATTERN_SOLID_BLUE);
}

static void bk_led_set_net_state(bk_led_net_state_e st)
{
    s_bk_net_state = st;
    bk_led_update();
}

static void bk_led_set_pwr_state(bk_led_pwr_state_e st)
{
    s_bk_pwr_state = st;
    bk_led_update();
}

static void bk_led_set_err_state(bk_led_err_state_e st)
{
    s_bk_err_state = st;
    bk_led_update();
}

static int bk_led_framework_handler(led_state_e state, void *param)
{
    (void)param;
    if (state == LED_STATE_ALL_OFF)
    {
        bk_led_apply_pattern(BK_PATTERN_OFF);
        GpioSetLevel(LED_W2_PIN, 0);
        GpioSetLevel(LED_W3_PIN, 0);
        GpioSetLevel(LED_W4_PIN, 0);
    }
    /* template uses custom LED patterns; consume framework updates. */
    return 0;
}

static void prv_battery_led_gpio_init(void)
{
    unsigned int mfp_led_w[] = {
        // MFP_REG(LED_GPIO_W1)   | LED_FUN_W1 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(LED_GPIO_W2) | LED_FUN_W2 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(LED_GPIO_W3) | LED_FUN_W3 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(LED_GPIO_W4) | LED_FUN_W4 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC};
    drvGpioPinMuxConfig(mfp_led_w);

    GPIOConfiguration cfg = {
        .pinDir = GPIO_OUT_PIN,
        .initLv = 0,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_NO_EDGE,
        .isr = NULL,
        .wu = NULL,
    };
    // GpioInitConfiguration(LED_W1_PIN, cfg);
    GpioInitConfiguration(LED_W2_PIN, cfg);
    GpioInitConfiguration(LED_W3_PIN, cfg);
    GpioInitConfiguration(LED_W4_PIN, cfg);
}

static void prv_battery_led_set_status(int percent)
{
    // ALL off
    // GpioSetLevel(LED_W1_PIN, 0);
    GpioSetLevel(LED_W2_PIN, 0);
    GpioSetLevel(LED_W3_PIN, 0);
    GpioSetLevel(LED_W4_PIN, 0);

    GpioSetLevel(LED_W2_PIN, percent >= 30);
    GpioSetLevel(LED_W3_PIN, percent >= 60);
    GpioSetLevel(LED_W4_PIN, percent >= 80);
}

static void bk_led_on_bearer_change(void)
{
    bk_led_set_net_state(BK_LED_NET_SWITCHING);
}

static void bk_led_on_net_ready(bool is_wifi)
{
    /* Treat successful network attach as OK; waiting/blink is only for connecting. */
    (void)is_wifi;
    bk_led_set_net_state(BK_LED_NET_SERVER_OK);
}

static void bk_led_on_net_fail(void)
{
    bk_led_set_net_state(BK_LED_NET_FAIL);
}

static void bk_led_on_server_ok(void)
{
    bk_led_set_net_state(BK_LED_NET_SERVER_OK);
}

static void bk_led_on_low_batt(bool low)
{
    bk_led_set_pwr_state(low ? BK_LED_PWR_LOW : BK_LED_PWR_NONE);
}

static void bk_led_on_charge(bool charging)
{
    bk_led_set_pwr_state(charging ? BK_LED_PWR_CHARGING : BK_LED_PWR_NONE);
}

static void bk_led_on_charge_full(void)
{
    bk_led_set_pwr_state(BK_LED_PWR_FULL);
}

static void bk_led_on_net_well_server_connect(void)
{
    if (template_handlers.net_mode)
    {
        bk_led_set_net_state(BK_LED_NET_WIFI_WAIT);
    }
    else
    {
        bk_led_set_net_state(BK_LED_NET_CELL_WAIT);
    }
}

static void customer_poweroff_playback_callback(audio_resource_id_t res_id, audio_playback_status_t status, int ret)
{
    if (res_id == AUDIO_RES_SYS_SHUTDOWN && status == AUDIO_PLAYBACK_STATUS_ENDED)
    {
        OSI_LOGI(TAG, "Power key long press shutdown - powering off");
        /* Publish Device OFF event per DUX TMS spec Section 4.2 */
        customer_dux_publish_device_power(0);
        osiThreadSleep(200);

        /* Stop audio, turn off indicators, wait for power key release, then power off */
        audio_resource_stop();
        framework_device_clear_poweroff_flag();
        osiSysPoweroff();
        while (1)
        {
            osiThreadSleep(100);
        }
    }
}

static int g_wifi_prov_in_progress = 0;
static int g_function_key_active = 0;
static int g_function_key_long_handled = 0;

#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
// static framework_wifi_config_mode_e s_wifi_config_cycle = FRAMEWORK_WIFI_CONFIG_MODE_WEB_AP;

static void prv_toggle_network_bearer(void)
{
    framework_network_bearer_e target =
        (template_handlers.net_mode == FRAMEWORK_NETWORK_BEARER_LTE) ? FRAMEWORK_NETWORK_BEARER_LTE : FRAMEWORK_NETWORK_BEARER_WIFI;
    // bk_led_on_bearer_change();
    int ret = framework_network_switch_bearer_sync(target);
    if (ret == 0)
    {
        // audio_resource_play(prv_audio_for_bearer(target), NULL);
        bk_led_on_net_well_server_connect();
    }
    else
    {
        OSI_LOGE(TAG, "Switch bearer failed: %d", ret);
        // audio_resource_play(AUDIO_RES_SYS_ERROR, NULL);
    }
}
static void prv_start_wifi_provisioning(void)
{
    if (framework_network_get_bearer() != FRAMEWORK_NETWORK_BEARER_WIFI)
    {
        if (framework_network_switch_bearer_sync(FRAMEWORK_NETWORK_BEARER_WIFI) != 0)
        {
            audio_resource_play(AUDIO_RES_SYS_ERROR, NULL);
            return;
        }
    }

    snprintf(ap_cfg_ssid, sizeof(ap_cfg_ssid), "template_%s", framework_network_get_imei());
    framework_wifi_config_request_t req = {
        .softap_ssid = ap_cfg_ssid,
        .mode = FRAMEWORK_WIFI_CONFIG_MODE_WEB_AP,
        .timeout_ms = 180000,
        .language = FRAMEWORK_WIFI_CONFIG_LANG_ENGLISH,
    };
    int ret = framework_network_wifi_config_start(&req);
    if (ret == 0)
    {
        g_wifi_prov_in_progress = FRAMEWORK_WIFI_CONFIG_MODE_WEB_AP;
    }
    else if (ret == -2)
    {
        OSI_LOGW(TAG, "WiFi config already running");
    }
    else
    {
        OSI_LOGE(TAG, "Start WiFi config failed: %d", ret);
        audio_resource_play(AUDIO_RES_SYS_ERROR, NULL);
    }
}

static int template_calc_file_md5(const char *path, char *md5_out33)
{
    if (!path || !md5_out33)
    {
        return -1;
    }

    int fd = fs_open(path, "rb");
    if (fd < 0)
    {
        return -2;
    }

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);

    uint8_t buf[4096];
    int read_len = 0;
    while ((read_len = fs_read(buf, 1, sizeof(buf), fd)) > 0)
    {
        mbedtls_md5_update_ret(&ctx, buf, read_len);
    }
    fs_close(fd);

    uint8_t md5_raw[16];
    mbedtls_md5_finish_ret(&ctx, md5_raw);
    mbedtls_md5_free(&ctx);

    for (int i = 0; i < 16; ++i)
    {
        sprintf(md5_out33 + (i * 2), "%02x", md5_raw[i]);
    }
    md5_out33[32] = '\0';
    return 0;
}

static void abnormal_work_callback(void *ctx)
{
    (void)ctx;
    uint8_t is_network_abnormal = 0;
    uint8_t current_time = 0;

    while (1)
    {
        if (framework_network_is_connected())
        {
            is_network_abnormal = 0;
            osiThreadSleepMs(10 * 1000);
            continue;
        }

        if (!is_network_abnormal)
        {
            is_network_abnormal = 1;
            current_time = 0;
        }

        if (current_time >= 3)
        {
            if (template_handlers.net_mode == FRAMEWORK_NETWORK_BEARER_LTE && (framework_network_sim_get_status() == false))
            {
                audio_resource_play_combo_async_mid_priority("", "no-sim.mp3", NULL, 1);
                bk_led_set_net_state(BK_LED_NET_SIM_MISSING);
            }
            else
            {
                audio_resource_play_combo_async_mid_priority("", "net-failed.mp3", NULL, 1);
            }
            current_time = 0;
        }
        current_time++;

        osiThreadSleepMs(10 * 1000);
    }
}

static void prv_handle_function_long_press(void)
{
    OSI_LOGI(TAG, "Function key long press: prov_in_progress=%d", g_wifi_prov_in_progress);
    if (g_wifi_prov_in_progress == 1)
    {
        OSI_LOGI(TAG, "WiFi provisioning already in progress");
        framework_network_wifi_config_stop();
        // audio_resource_play_combo_async_mid_priority("", "cfg-net-exit.mp3", NULL, 1);
        g_wifi_prov_in_progress = 0;
        return;
    }
    prv_start_wifi_provisioning();
}

static void prv_function_long_press_work_callback(void *param)
{
    (void)param;
    prv_handle_function_long_press();
}

static void prv_function_long_press_timer_callback(void *param)
{
    osiTimerStop(template_handlers.function_long_press_timer);
    osiTimerDelete(template_handlers.function_long_press_timer);
    template_handlers.function_long_press_timer = NULL;
}
#endif /* CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET */

static void prv_network_bearer_press_work_callback(void *param)
{
    (void)param;
    // OSI_LOGI(TAG, "Volume up key long pressed");
    prv_toggle_network_bearer();
}

static void prv_function_press_timer_callback(void *param)
{
    (void)param;
    osiTimerStop(template_handlers.function_press_timer);
    osiTimerDelete(template_handlers.function_press_timer);
    template_handlers.function_press_timer = NULL;
}

static void prv_vol_up_press_timer_callback(void *param)
{
    uint8_t vol = 0;

    osiTimerStop(template_handlers.vol_up_press_timer);
    osiTimerDelete(template_handlers.vol_up_press_timer);
    template_handlers.vol_up_press_timer = NULL;
    vol = audio_volume_get() + VOL_STEP;
    if (vol < VOL_MAX)
    {
        audio_volume_set(vol);
        framework_device_config_set_volume(vol);
        audio_resource_play_try_nothing_timeout(AUDIO_RES_VOL_UP, NULL);
    }
    else
    {
        audio_resource_play_try_nothing_timeout(AUDIO_RES_VOL_MAX, NULL);
    }
}

static void prv_toggle_network_bearer_callback(int key)
{
    if (key == MSG_KEY_FUNCTION)
    {
        if (template_handlers.vol_up_press_timer)
        {
            osiTimerStop(template_handlers.vol_up_press_timer);
            osiTimerDelete(template_handlers.vol_up_press_timer);
            template_handlers.vol_up_press_timer = NULL;
            template_handlers.net_mode = !template_handlers.net_mode;
            if (template_handlers.net_mode)
            {
                audio_resource_play_combo_async_mid_priority("", "using-net-wifi.mp3", NULL, 1);
            }
            else
            {
                audio_resource_play_combo_async_mid_priority("", "using-net-gprs.mp3", NULL, 1);
            }
            bk_led_on_bearer_change();
            if (template_handlers.network_bearer_work)
            {
                osiWorkEnqueue(template_handlers.network_bearer_work, osiSysWorkQueueLowPriority());
            }
        }
        else
        {
            if (template_handlers.function_press_timer == NULL)
            {
                template_handlers.function_press_timer = osiTimerCreate(prv_function_press_timer_callback, NULL);
                osiTimerStart(template_handlers.function_press_timer, 650);
            }
            else
            {
                osiTimerStop(template_handlers.function_press_timer);
                osiTimerStart(template_handlers.function_press_timer, 650);
            }
        }
    }
    else if (key == MSG_KEY_VOL_UP)
    {
        if (template_handlers.function_press_timer)
        {
            osiTimerStop(template_handlers.function_press_timer);
            osiTimerDelete(template_handlers.function_press_timer);
            template_handlers.function_press_timer = NULL;
            template_handlers.net_mode = !template_handlers.net_mode;
            if (template_handlers.net_mode)
            {
                audio_resource_play_combo_async_mid_priority("", "using-net-wifi.mp3", NULL, 1);
            }
            else
            {
                audio_resource_play_combo_async_mid_priority("", "using-net-gprs.mp3", NULL, 1);
            }
            if (template_handlers.net_mode)
            {
                audio_resource_play_combo_async_mid_priority("", "using-net-wifi.mp3", NULL, 1);
            }
            else
            {
                audio_resource_play_combo_async_mid_priority("", "using-net-gprs.mp3", NULL, 1);
            }
            bk_led_on_bearer_change();
            if (template_handlers.network_bearer_work)
            {
                osiWorkEnqueue(template_handlers.network_bearer_work, osiSysWorkQueueLowPriority());
            }
        }
        else
        {
            if (template_handlers.vol_up_press_timer == NULL)
            {
                template_handlers.vol_up_press_timer = osiTimerCreate(prv_vol_up_press_timer_callback, NULL);
                osiTimerStart(template_handlers.vol_up_press_timer, 650);
            }
            else
            {
                osiTimerStop(template_handlers.vol_up_press_timer);
                osiTimerStart(template_handlers.vol_up_press_timer, 650);
            }
        }
    }
}

static void template_abnormal_work_timer_callback(void *param)
{
    (void)param;
    /* Enqueue work to system work queue (non-blocking) */
    if (template_handlers.abnormal_work_timer && template_handlers.abnormal_work_flag < 3)
    {
        template_handlers.abnormal_work_flag++;
        bk_led_on_net_fail();
        audio_resource_play_combo_async_mid_priority_TryPut("", "server-failed.mp3", NULL, 1);
    }
}
// static uint32_t prv_build_battery_combo(uint8_t percent, audio_resource_id_t *out_ids, uint32_t max_ids)
// {
//     if (!out_ids || max_ids == 0) {
//         return 0;
//     }
//     audio_resource_id_t level_id = AUDIO_RES_BAT_MEDIUM;
//     if (percent >= 95) {
//         level_id = AUDIO_RES_BAT_FULL;
//     } else if (percent >= 75) {
//         level_id = AUDIO_RES_BAT_HIGH;
//     } else if (percent >= 50) {
//         level_id = AUDIO_RES_BAT_MEDIUM;
//     } else if (percent >= 20) {
//         level_id = AUDIO_RES_BAT_LOW;
//     } else {
//         level_id = AUDIO_RES_BAT_CRITICAL;
//     }
//     out_ids[0] = level_id;
//     return 1;
// }

int customer_msg_handler(framework_msg_t *msg)
{
    static uint32_t last_fail_report = 0;
    if (msg == NULL)
    {
        return -1;
    }
    char sn[32] = {0};
    framework_device_config_get_sn(sn, sizeof(sn));
    switch (msg->id)
    {
    case MSG_KEY_POWER:
        if (msg->param1 == KEY_STATE_LONG_PRESS)
        {
            OSI_LOGI(TAG, "Power key long pressed: preparing power off");

            /* 1. Report Device_ON_OFF = "0" to TMS if MQTT connected */
               customer_dux_publish_device_power(0);
                osiThreadSleepMs(200); /* Allow MQTT packet to flush over socket */

            /* 2. Play shutdown chime with urgent priority & shut down on completion */
            audio_resource_play_with_priority(AUDIO_RES_SYS_SHUTDOWN,
                                              customer_poweroff_playback_callback,
                                              AUDIO_PLAY_PIRORITY_URGENT);
            return 0;
        }
        else if (msg->param1 == KEY_STATE_DOWN)
        {
            OSI_LOGI(TAG, "Power key pressed");
            return 0;
        }
        break;
    case MSG_KEY_FUNCTION:
#if CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET
        OSI_LOGI(TAG, "Func key event: long_handled=%d active: %d", g_function_key_long_handled, g_function_key_active);
        if (msg->param1 == KEY_STATE_DOWN)
        {
            g_function_key_active = 1;
            g_function_key_long_handled = 0;
            if (customer_payment_replay_last() == 0)
            {
                OSI_LOGI(TAG, "Function key: Replayed last transaction");
            }
            return 0;
        }
        else if (msg->param1 == KEY_STATE_LONG_PRESS)
        {
            if (g_function_key_active && !g_function_key_long_handled)
            {
                g_function_key_long_handled = 1;
                // if(template_handlers.function_long_press_timer == NULL)
                {
                    if (!g_wifi_prov_in_progress)
                    {
                        // template_handlers.function_long_press_timer = osiTimerCreate(prv_function_long_press_timer_callback, NULL);
                        // osiTimerStart(template_handlers.function_long_press_timer, 10000);
                        audio_resource_play_combo_async_mid_priority("", "cfg-net-ap.mp3", NULL, 1);
                    }
                    else
                    {
                        audio_resource_play_combo_async_mid_priority("", "cfg-net-exit.mp3", NULL, 1);
                    }
                }
                if (template_handlers.function_long_press_work)
                {
                    osiWorkEnqueue(template_handlers.function_long_press_work, osiSysWorkQueueLowPriority());
                }
                template_handlers.net_mode = FRAMEWORK_NETWORK_BEARER_WIFI;
            }
            return 0;
        }
        else if (msg->param1 == KEY_STATE_UP)
        {
            if (g_function_key_active)
            {
                prv_toggle_network_bearer_callback(MSG_KEY_FUNCTION);
                g_function_key_active = 0;
            }
            return 0;
        }
#else
        if (msg->param1 == KEY_STATE_DOWN)
        {
            if (customer_payment_replay_last() == 0)
            {
                OSI_LOGI(TAG, "Function key: Replayed last transaction");
                return 0;
            }
            uint8_t percent = framework_battery_get_percent();
            OSI_LOGI(TAG, "Function key pressed battery percent=%u", percent);
            audio_resource_id_t combo_ids[4] = {0};
            combo_ids[0] = AUDIO_RES_SYS_STARTUP;
            uint32_t combo_count = prv_build_battery_combo(percent, &combo_ids[1], sizeof(combo_ids) / sizeof(combo_ids[0]) - 1);
            if (combo_count == 0)
            {
                OSI_LOGW(TAG, "customer_msg_handler: Battery combo build failed");
                return 0;
            }
            OSI_LOGI(TAG, "customer_msg_handler: Battery percent=%u, combo_count=%lu", percent, (unsigned long)combo_count + 1);
            int play_ret = audio_resource_play_stream(combo_ids, combo_count + 1, NULL);
            if (play_ret != 0)
            {
                OSI_LOGE(TAG, "customer_msg_handler: combo playback failed (%d)", play_ret);
            }
            return 0;
        }
#endif
        break;
    case MSG_KEY_VOL_UP:
        if (msg->param1 == KEY_STATE_DOWN)
        {
            uint8_t vol = framework_device_config_get_volume();
            if (vol < 7)
            {
                vol++;
                audio_resource_play_mid_priority(AUDIO_RES_VOL_UP, NULL);
            }
            else
            {
                audio_resource_play_mid_priority(AUDIO_RES_VOL_MAX, NULL);
            }
            framework_device_config_set_volume(vol);
            audio_volume_set(vol);
            Paytm_LCD_Set_Volume_Levl(vol);
            appAllrefresh();
            // appLcdDisplayEnableALL(true);
            OSI_LOGI(TAG, "Volume up key pressed, new vol=%u", vol);
        }
        return 0;
    case MSG_KEY_VOL_DOWN:
        if (msg->param1 == KEY_STATE_DOWN)
        {
            uint8_t vol = framework_device_config_get_volume();
            if (vol > 0)
            {
                vol--;
                audio_resource_play_mid_priority(AUDIO_RES_VOL_DOWN, NULL);
            }
            else
            {
                audio_resource_play_mid_priority(AUDIO_RES_VOL_MIN, NULL);
            }
            framework_device_config_set_volume(vol);
            audio_volume_set(vol);
            Paytm_LCD_Set_Volume_Levl(vol);
            appAllrefresh();
            // appLcdDisplayEnableALL(true);
            OSI_LOGI(TAG, "Volume down key pressed, new vol=%u", vol);
        }
        return 0;
    case MSG_CHARGE_PLUGIN:
    {
        OSI_LOGI(TAG, "Charger connected");
        audio_resource_play(AUDIO_RES_CHG_CONNECTED, NULL);
        if (!template_handlers.battery_full)
        {
            bk_led_on_charge(true);
        }
        // 充电状态上报 (DUX TMS SBHealthStatus)
        customer_dux_publish_charging_status(1);
        return 0;
    }
    case MSG_CHARGE_PLUGOUT:
    {
        OSI_LOGI(TAG, "Charger disconnected");
        template_handlers.battery_full = false;
        audio_resource_play(AUDIO_RES_CHG_DISCONNECTED, NULL);
        bk_led_on_charge(false);
        // 充电状态上报 (DUX TMS SBHealthStatus)
        customer_dux_publish_charging_status(0);
        return 0;
    }
    case MSG_CHARGE_LOW_POWER:
        OSI_LOGI(TAG, "Battery low warning");
        // audio_resource_play(AUDIO_RES_BAT_LOW, NULL);
        if (framework_charger_is_connected())
        {
            OSI_LOGI(TAG, "Charger connected during low power warning, aborting shutdown");
            return 0;
        }
        bk_led_on_low_batt(true);
        audio_resource_play(AUDIO_RES_BAT_LOW, NULL);
        if (msg->param2 == -1)
        {
            osiThreadSleep(2000);
            audio_resource_play_with_priority(AUDIO_RES_SYS_SHUTDOWN, customer_poweroff_playback_callback, AUDIO_PLAY_PIRORITY_URGENT);
        }
        return 0;
    case MSG_CHARGE_FULL:
        OSI_LOGI(TAG, "Battery full");
        if (template_handlers.battery_full)
        {
            return 0;
        }
        template_handlers.battery_full = true;
        bk_led_on_low_batt(false);
        bk_led_on_charge_full();
        bk_led_on_charge(false);
        audio_resource_play(AUDIO_RES_BAT_FULL, NULL);
        return 0;
    case MSG_CHARGE_PERCENT_CHANGE:
    {
        uint8_t bat_p = (uint8_t)msg->param1;
        OSI_LOGI(TAG, "Battery percent=%u", bat_p);
        prv_battery_led_set_status(bat_p);
        int p_levl = (bat_p >= 80) ? 4 : (bat_p >= 60) ? 3
                                     : (bat_p >= 40)   ? 2
                                     : (bat_p >= 20)   ? 1
                                                       : 0;
        Paytm_LCD_Set_Power_Levl(p_levl);
        appAllrefresh();
        // appLcdDisplayEnableALL(true);
        if (bat_p <= 15)
        {
            bk_led_on_low_batt(true);
        }
        else if (s_bk_pwr_state == BK_LED_PWR_LOW)
        {
            bk_led_on_low_batt(false);
        }
        if (template_handlers.battery_full)
        {
            return 0;
        }
        if (framework_battery_is_usb_plugin())
        {
            if ((bat_p >= 99) && (((bat_p > (uint8_t)msg->param2)) || ((uint8_t)msg->param2 == 0xFF)))
            {
                bk_led_on_low_batt(false);
                bk_led_on_charge_full();
                bk_led_on_charge(false);
                audio_resource_play(AUDIO_RES_BAT_FULL, NULL);
                template_handlers.battery_full = true;
            }
        }
        return 0;
    }
    case MSG_NET_WIFI_CONFIG_SUCCESS:
        OSI_LOGI(TAG, "WiFi config success (mode=%lu)", (unsigned long)msg->param1);
        audio_resource_play_combo_async_mid_priority("", "cfg-net-success.mp3", NULL, 1);
        g_wifi_prov_in_progress = 0;
    case MSG_NET_CONNECTED:
        OSI_LOGI(TAG, "Network connected");
        Paytm_LCD_Set_Signal_Levl(4);
        appAllrefresh();
        // appLcdDisplayEnableALL(true);
        if (g_wifi_prov_in_progress == 0)
        {
            audio_resource_play_combo_async_mid_priority("", "net-success.mp3", NULL, 1);
        }
        bk_led_on_net_well_server_connect();
        g_wifi_prov_in_progress = 0;
        // bk_led_on_net_ready(framework_network_get_bearer() == FRAMEWORK_NETWORK_BEARER_WIFI);

        /* Trigger asynchronous NTP sync immediately after internet/network connection */
        OSI_LOGI("Test-Time", "[MSG_NET_CONNECTED] Network connected! Triggering framework_network_ntp_sync_now(false)");
        framework_network_ntp_sync_now(false);

        /* Launch TMS registration in a dedicated thread with 12KB stack for HTTPS / TLS */
        OSI_LOGI(TAG, "Launching TMS Registration background task...");
        osiThread_t *reg_th = osiThreadCreate("tms_reg", tms_registration_task, NULL, 120, 12 * 1024);
        if (!reg_th)
        {
            OSI_LOGE(TAG, "Failed to create TMS registration thread");
        }

        return 0;
    case MSG_NET_DISCONNECTED:
        OSI_LOGI(TAG, "Network disconnected");
        Paytm_LCD_Set_Signal_Levl(0);
        appAllrefresh();
        // appLcdDisplayEnableALL(true);
        if (framework_network_get_state() == NETWORK_STATE_DISCONNECTED)
        {
            audio_resource_play(AUDIO_RES_NET_DISCONNECTED, NULL);
        }
        bk_led_on_net_fail();
        return 0;
    case MSG_NET_WIFI_CONFIG_TIMEOUT:
        OSI_LOGW(TAG, "WiFi config timeout (mode=%lu)", (unsigned long)msg->param1);
        audio_resource_play_combo_async_mid_priority("", "cfg-net-failed.mp3", NULL, 1);
        g_wifi_prov_in_progress = 0;
        return 0;
    case MSG_NET_WIFI_CONFIG_ERROR:
        OSI_LOGE(TAG, "WiFi config error (mode=%lu, status=%ld)",
                 (unsigned long)msg->param1, (long)msg->param2);
        audio_resource_play_combo_async_mid_priority("", "cfg-net-failed.mp3", NULL, 1);
        g_wifi_prov_in_progress = 0;
        return 0;
    case MSG_MQTT_DISCONNECTED:
        OSI_LOGI(TAG, "MQTT disconnected");
        if (template_handlers.abnormal_work_timer == NULL)
        {
            template_handlers.abnormal_work_timer = osiTimerCreate(template_abnormal_work_timer_callback, NULL);
            if (!template_handlers.abnormal_work_timer)
            {
                OSI_LOGE(TAG, TAG " Failed to create network disconner timer");
            }
            osiTimerStart(template_handlers.abnormal_work_timer, 1000 * 60);
        }
        return 0;
        break;
    case MSG_MQTT_CONNECTED:
        OSI_LOGI(TAG, "MQTT connected");
        if (template_handlers.abnormal_work_timer != NULL)
        {
            osiTimerStop(template_handlers.abnormal_work_timer);
            osiTimerDelete(template_handlers.abnormal_work_timer);
            template_handlers.abnormal_work_timer = NULL;
            if (template_handlers.abnormal_work_flag > 0)
            {
                template_handlers.abnormal_work_flag = 0;
                audio_resource_play_combo_async_mid_priority("", "server-success.mp3", NULL, 1);
            }
        }
        else
        {
            audio_resource_play_combo_async_mid_priority("", "server-success.mp3", NULL, 1);
        }
        bk_led_on_server_ok();

        /* Gatekeeper: publish Device ON to SBHealthStatus if NTP is already synced */
        customer_dux_check_and_publish_device_on();
        return 0;
    case MSG_MQTT_CONNECT_FAILED:
        OSI_LOGI(TAG, "MQTT connect failed");
        if (last_fail_report == 0 || osiGetTicks() - last_fail_report > 60000 / 5)
        {
            last_fail_report = osiGetTicks();
            bk_led_on_net_fail();
            audio_resource_play_combo_async_mid_priority("", "server-failed.mp3", NULL, 1);
        }
        else
        {
            OSI_LOGI(TAG, "MQTT connect failed recently, suppressing repeated alert");
            return 0;
        }

        return 0;
    case MSG_NET_NTP_SUCCESS:
        OSI_LOGI("Test-Time", "[MSG_NET_NTP_SUCCESS] NTP sync SUCCESS event! server_idx=%lu, timestamp=%lu. Setting ntp_sync=1",
                 (unsigned long)msg->param1, (unsigned long)msg->param2);
        customer_set_report_ntp_sync(1);

        /* Gatekeeper: if MQTT is already connected, publish Device ON with the valid timestamp now */
        customer_dux_check_and_publish_device_on();
        return 0;
    case MSG_NET_NTP_FAILURE:
        OSI_LOGW("Test-Time", "[MSG_NET_NTP_FAILURE] NTP sync FAILED event! failed_idx=%lu, err=%ld",
                 (unsigned long)msg->param1, (long)msg->param2);
        return 0;
    case MSG_HTTP_DOWNLOAD_START:
    case MSG_HTTP_DOWNLOAD_PROGRESS:
    case MSG_HTTP_DOWNLOAD_SUCCESS:
    case MSG_HTTP_DOWNLOAD_FAIL:
    {
        http_download_type_t dl_type = (http_download_type_t)msg->param1;
        if (dl_type == HTTP_DOWNLOAD_TYPE_RES_OTA)
        {
            template_mqtt_handle_http_msg(msg);
            return 0;
        }
        else if (dl_type == HTTP_DOWNLOAD_TYPE_FW_OTA)
        {
            switch (msg->id)
            {
            case MSG_HTTP_DOWNLOAD_START:
                OSI_LOGI(TAG, "FW OTA download started");
                break;
            case MSG_HTTP_DOWNLOAD_PROGRESS:
                OSI_LOGI(TAG, "FW OTA download progress: %d%%", msg->param2);
                break;
            case MSG_HTTP_DOWNLOAD_SUCCESS:
            {
                const char *path = (const char *)(uintptr_t)msg->param3;
                OSI_LOGI(TAG, "FW OTA download success file: %s", path ? path : "");
                if (path && path[0])
                {
                    char md5[33] = {0};
                    if (template_calc_file_md5(path, md5) == 0)
                    {
                        OSI_LOGI(TAG, "FW OTA file MD5: %s", md5);
                    }
                    else
                    {
                        OSI_LOGE(TAG, "FW OTA file MD5 calc failed");
                    }
                }
                bool fota_ret = fupdateAppSetReady(path ? path : "");
                if (fota_ret)
                {
                    audio_resource_play_combo_async_mid_priority("", "upgrade-success.mp3", NULL, 1);
                    OSI_LOGI(TAG, "Firmware update set ready successfully");
                    osiThreadSleep(1000);
                    osiSysReset();
                }
                else
                {
                    OSI_LOGE(TAG, "Failed to set firmware update ready");
                    audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
                }
                break;
            }
            case MSG_HTTP_DOWNLOAD_FAIL:
                audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
                OSI_LOGE(TAG, "FW OTA download failed (err=%d)", msg->param2);
                break;
            default:
                break;
            }
            return 0;
        }
        if (dl_type == HTTP_DOWNLOAD_TYPE_WIFI_FW_OTA)
        {
            switch (msg->id)
            {
            case MSG_HTTP_DOWNLOAD_START:
                OSI_LOGI(TAG, "FW OTA download started");
                break;
            case MSG_HTTP_DOWNLOAD_PROGRESS:
                OSI_LOGI(TAG, "FW OTA download progress: %d%%", msg->param2);
                break;
            case MSG_HTTP_DOWNLOAD_SUCCESS:
            {
                const char *path = (const char *)(uintptr_t)msg->param3;
                OSI_LOGI(TAG, "FW OTA download success file: %s", path ? path : "");
                if (path && path[0])
                {
                    char md5[33] = {0};
                    if (template_calc_file_md5(path, md5) == 0)
                    {
                        OSI_LOGI(TAG, "FW OTA file MD5: %s", md5);
                    }
                    else
                    {
                        OSI_LOGE(TAG, "FW OTA file MD5 calc failed");
                    }
                }
                int wifi_update(char *binaryPath, int snyc);
                if (wifi_update((char *)path, false) == 0)
                {
                    audio_resource_play_combo_async_mid_priority("", "upgrade-success.mp3", NULL, 1);
                }
                else
                {
                    OSI_LOGE(TAG, "Failed to set firmware update ready");
                    audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
                }
                break;
            }
            case MSG_HTTP_DOWNLOAD_FAIL:
                audio_resource_play_combo_async_mid_priority("", "upgrade-failed.mp3", NULL, 1);
                OSI_LOGE(TAG, "FW OTA download failed (err=%d)", msg->param2);
                break;
            default:
                break;
            }
            return 0;
        }

        break;
    }
    case MSG_UNZIP_START:
    case MSG_UNZIP_PROGRESS:
    case MSG_UNZIP_SUCCESS:
    case MSG_UNZIP_FAIL:
        template_mqtt_handle_unzip_msg(msg);
        return 0;
    default:
        break;
    }
    return -1;
}

void customer_init(void)
{
    osiExceptionDumpEnable(false);
    osiSetPMEnableSleep(0); // add by vipin: disable PM sleep so CPU/network stays awake and receives MQTT in real-time
    appLcdInit();           // LCD init
    appLcdenable();         // LCD enable
    uint8_t init_bat = framework_battery_get_percent();
    int init_p_levl = (init_bat >= 80) ? 4 : (init_bat >= 60) ? 3
                                         : (init_bat >= 40)   ? 2
                                         : (init_bat >= 20)   ? 1
                                                              : 0;
    Paytm_LCD_Set_Power_Levl(init_p_levl);
    appAllrefresh();
    // appLcdDisplayEnableALL(true);
    OSI_LOGI(TAG, "========================================");
    OSI_LOGI(TAG, "template Customer Initialization");
    OSI_LOGI(TAG, "========================================");
    int vol_list[VOL_MAX] = {0xffffdc00, 0xffffef65, 0xfffff1d2, 0xfffff43f, 0xfffff6ab, 0xfffff918, 0xfffffb84, 0xfffffe6d};

    framework_audio_set_volume_gain_list(vol_list, VOL_MAX);
    audio_volume_set(framework_device_config_get_volume());

    framework_led_clear_all();
    prv_battery_led_gpio_init();
    framework_led_register_state_handler(bk_led_framework_handler, NULL);
    if (framework_network_sim_get_status() == false)
    {
        bk_led_set_net_state(BK_LED_NET_SIM_MISSING);
    }
    else
    {
        bk_led_on_net_well_server_connect();
    }
    char imei[32] = {0};
    snprintf(imei, sizeof(imei), "BP02%s", framework_network_get_imei());
    OSI_LOGI(TAG, "Setting device SN to IMEI: %s", imei);
    framework_device_config_set_sn(imei);
    if (customer_battery_config_register() != 0)
    {
        OSI_LOGE(0, "TEMPLATE: Failed to register battery config");
    }
    else
    {
        OSI_LOGI(0, "TEMPLATE: Battery config registered");
    }
    OSI_LOGI(TAG, "Playing welcome sound");
    if ((strcmp(audio_resource_get_language_prefix(), AUDIO_LANG_FEMALE_PATH)) && (strcmp(audio_resource_get_language_prefix(), AUDIO_LANG_MALE_PATH)))
    {
        audio_resource_set_language_prefix(AUDIO_LANG_FEMALE_PATH);
    }
    // audio_resource_set_language_prefix(AUDIO_LANG_FEMALE_PATH);
    audio_resource_play_high_priority(AUDIO_RES_SYS_STARTUP, NULL);
    framework_msg_register_callback(customer_msg_handler);
    audio_resource_play_combo_async_mid_priority("", "using-net-gprs.mp3", NULL, 0);
    if (framework_network_sim_get_status() == false)
    {
        audio_resource_play_combo_async_mid_priority("", "no-sim.mp3", NULL, 0);
    }

    template_handlers.network_bearer_work = osiWorkCreate(prv_network_bearer_press_work_callback,
                                                          NULL, NULL);

    template_handlers.function_long_press_work = osiWorkCreate(prv_function_long_press_work_callback,
                                                               NULL, NULL);
    if (!template_handlers.function_long_press_work)
    {
        OSI_LOGE(TAG, "Failed to create function long press work");
    }

    osiThreadCreate("template_netmonit", abnormal_work_callback, NULL, 130, 1024);
    OSI_LOGI(TAG, "========================================");
    OSI_LOGI(TAG, "template Initialization Complete");
    OSI_LOGI(TAG, "========================================");
}
