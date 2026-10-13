/**
 * @file framework_led.c
 * @brief Framework LED管理模块实现
 * @details 参考app_led.c设计,提供LED状态管理和客户回调机制
 */

#include <string.h>
#include "framework_config.h"
#include "framework_led.h"
#include "framework_log.h"
#include "osi_api.h"
#include "gpio_api.h"
#include "drv_iomux.h"
#include "drv_def.h"

// ============================================================================
// 宏定义
// ============================================================================
#define MAX_LED_NUM  3  // LED数量

#define LED_BLINK_FAST   500   // 快闪间隔(ms)
#define LED_BLINK_SLOW   1000  // 慢闪间隔(ms)
#define LED_ON_TIME      300   // 闪烁点亮时间(ms)
#define LED_OFF_TIME_FAST  200  // 快闪熄灭时间 (500-300=200ms)
#define LED_OFF_TIME_SLOW  1700 // 慢闪熄灭时间 (2000-300=1700ms)

//======================================================================
// 类型定义
// ============================================================================
typedef struct {
    uint32_t id;         // GPIO引脚ID
    bool cur_level;      // 当前电平状态
    uint32_t interval;   // 闪烁间隔(ms)
} led_pin_t;

// ============================================================================
// 静态变量
// ============================================================================
static led_pin_t s_led_list[MAX_LED_NUM] = {0};  // LED引脚列表
static uint32_t s_cur_state = 0;                 // 当前LED状态掩码
static uint32_t s_last_show_state = 0;           // 上次显示的状态
static osiTimer_t *s_led_timer = NULL;           // LED闪烁定时器

static led_state_handler_t s_state_handler = NULL;  // 客户状态处理回调
static void *s_handler_param = NULL;                 // 回调用户参数

// ============================================================================
// 内部函数声明
// ============================================================================
static void prv_gpio_init(void);
static void prv_led_timer_handler(void *ctx);
static void prv_led_set_status(int led_pin, int interval_ms);
static void prv_led_status_update(void);
static void prv_led_all_off(void);

// ============================================================================
// 内部函数实现
// ============================================================================

/**
 * @brief 初始化LED GPIO引脚复用
 */
static void prv_gpio_init(void)
{
    unsigned int mfp_spk[] = {
        MFP_REG(LED_RED_PINMUX)   | LED_RED_FUN | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(LED_GREEN_PINMUX) | LED_GREEN_FUN | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(LED_BLUE_PINMUX)  | LED_BLUE_FUN | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    drvGpioPinMuxConfig(mfp_spk);
}

/**
 * @brief LED闪烁定时器回调
 * @param ctx LED引脚结构体指针
 */
static void prv_led_timer_handler(void *ctx)
{
    led_pin_t *led_pin = (led_pin_t *)ctx;

    if (led_pin->cur_level == true) {
        // 当前亮 -> 熄灭
        led_pin->cur_level = false;
        GpioSetLevel(led_pin->id, led_pin->cur_level);
        osiTimerStart(s_led_timer, led_pin->interval);
    } else {
        // 当前灭 -> 点亮
        led_pin->cur_level = true;
        GpioSetLevel(led_pin->id, led_pin->cur_level);
        osiTimerStart(s_led_timer, LED_ON_TIME);
    }
}

/**
 * @brief 设置LED显示状态 (默认实现)
 * @param led_pin LED引脚ID (LED_RED_PIN/LED_GREEN_PIN/LED_BLUE_PIN)
 * @param interval_ms 闪烁间隔 (0=常亮, 500=快闪, 1000=慢闪)
 */
static void prv_led_set_status(int led_pin, int interval_ms)
{
    // 停止之前的定时器
    if (s_led_timer) {
        osiTimerStop(s_led_timer);
        osiTimerDelete(s_led_timer);
        s_led_timer = NULL;
    }

    // 关闭所有LED,点亮目标LED
    for (int i = 0; i < MAX_LED_NUM; i++) {
        if (s_led_list[i].id == (uint32_t)led_pin) {
            GpioSetLevel(s_led_list[i].id, true);  // 点亮目标LED

            // 设置闪烁
            if (interval_ms == LED_BLINK_FAST) {
                // 快闪: 300ms亮 + 200ms灭
                s_led_list[i].interval = LED_OFF_TIME_FAST;
                s_led_list[i].cur_level = true;
                s_led_timer = osiTimerCreate(prv_led_timer_handler, &s_led_list[i]);
                osiTimerStart(s_led_timer, LED_BLINK_FAST);
            } else if (interval_ms == LED_BLINK_SLOW) {
                // 慢闪: 300ms亮 + 1700ms灭
                s_led_list[i].interval = LED_OFF_TIME_SLOW;
                s_led_list[i].cur_level = true;
                s_led_timer = osiTimerCreate(prv_led_timer_handler, &s_led_list[i]);
                osiTimerStart(s_led_timer, LED_ON_TIME);
            }
            // interval_ms == 0: 常亮,不启动定时器
        } else {
            GpioSetLevel(s_led_list[i].id, false);  // 关闭其他LED
        }
    }
}

/**
 * @brief 关闭所有LED
 */
static void prv_led_all_off(void)
{
    if (s_led_timer) {
        osiTimerStop(s_led_timer);
        osiTimerDelete(s_led_timer);
        s_led_timer = NULL;
    }

    for (int i = 0; i < MAX_LED_NUM; i++) {
        GpioSetLevel(s_led_list[i].id, false);
    }
}

/**
 * @brief 更新LED显示状态 (优先级处理 + 回调机制)
 * @details 状态优先级: bit0-2(异常) > bit3-4(充电) > bit5-6(网络)
 */
static void prv_led_status_update(void)
{
    led_state_e show_state = 0;


    // 从低位到高位扫描,找到最高优先级状态
    for (int i = 0; i < 8; i++) {
        if (s_cur_state & (0x01 << i)) {
            show_state = (led_state_e)(0x01 << i);
            break;
        }
    }

    // 状态未变化或无状态,不处理
    if (show_state == 0 || show_state == s_last_show_state) {
        return;
    }

    // 先调用客户回调
    if (s_state_handler != NULL) {
        int ret = s_state_handler(show_state, s_handler_param);
        if (ret == 0) {
            // 客户已处理,不执行默认GPIO控制
            s_last_show_state = show_state;
            return;
        }
    }

    // 执行默认LED控制逻辑
    switch (show_state) {
    case LED_STATE_EXCEPTION_SERVER:
        // bit0: 服务器异常 -> 红灯快闪(500ms)
        prv_led_set_status(LED_RED_PIN, LED_BLINK_FAST);
        break;

    case LED_STATE_EXCEPTION_NET:
        // bit1: 网络异常 -> 红灯慢闪(1000ms)
        prv_led_set_status(LED_RED_PIN, LED_BLINK_SLOW);
        break;

    case LED_STATE_EXCEPTION_POWER:
        // bit2: 电源异常 -> 红灯常亮
        prv_led_set_status(LED_RED_PIN, 0);
        break;

    case LED_STATE_CHARGING:
        // bit3: 充电中 -> 蓝灯慢闪(1000ms)
        prv_led_set_status(LED_BLUE_PIN, LED_BLINK_SLOW);
        break;

    case LED_STATE_CHARGE_FULL:
        // bit4: 充电完成 -> 蓝灯常亮
        prv_led_set_status(LED_BLUE_PIN, 0);
        break;

    case LED_STATE_NET_CONNECTING:
        // bit5: 网络连接中 -> 绿灯快闪(500ms)
        prv_led_set_status(LED_GREEN_PIN, LED_BLINK_FAST);
        break;

    case LED_STATE_NET_CONNECTED:
        // bit6: 网络已连接 -> 绿灯常亮
        prv_led_set_status(LED_GREEN_PIN, 0);
        break;

    default:
        break;
    }

    s_last_show_state = show_state;
}

// ============================================================================
// 对外接口实现
// ============================================================================

/**
 * @brief 初始化LED管理模块
 */
int framework_led_init(void)
{
    int pin_list[MAX_LED_NUM] = {LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN};

    GPIOConfiguration cfg = {
        .pinDir = GPIO_OUT_PIN,
        .initLv = 0,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_NO_EDGE,
        .isr = NULL,
        .wu = NULL,
    };

    // 初始化LED引脚列表
    for (int i = 0; i < MAX_LED_NUM; i++) {
        s_led_list[i].id = pin_list[i];
        s_led_list[i].cur_level = false;
        s_led_list[i].interval = 0;
        GpioInitConfiguration(s_led_list[i].id, cfg);
    }

    // 配置引脚复用
    prv_gpio_init();

    // 初始化状态
    s_cur_state = 0;
    s_last_show_state = 0;
    s_state_handler = NULL;
    s_handler_param = NULL;

    return 0;
}

/**
 * @brief 设置LED状态 (OR操作)
 */
int framework_led_set_state(uint32_t state)
{
    s_cur_state |= (uint32_t)state;
    prv_led_status_update();
    return 0;
}

/**
 * @brief 清除LED状态 (AND NOT操作)
 */
int framework_led_clear_state(uint32_t state)
{
    s_cur_state &= (~((uint32_t)state)) & 0xFFFFFFFF;
    prv_led_status_update();
    return 0;
}

/**
 * @brief 清除所有LED状态
 */
int framework_led_clear_all(void)
{
    s_cur_state = 0;
    s_last_show_state = 0;
    if(s_state_handler && s_state_handler(LED_STATE_ALL_OFF, s_handler_param) == 0){
        return 0;
    }
    prv_led_all_off();
    return 0;
}

void framework_led_set_blink(uint32_t pin, uint32_t blink)
{
    return prv_led_set_status(pin, blink);
}

void framework_led_all_off(void)
{
    return prv_led_all_off();
}

/**
 * @brief 注册LED状态处理回调
 */
int framework_led_register_state_handler(led_state_handler_t handler, void *param)
{
    s_state_handler = handler;
    s_handler_param = param;
    return 0;
}

static osiTimer_t *ledTimer = NULL;
void prv_Led_factory_Handle(void *ctx){
    static int idx = 0;
    osiTimerStop(ledTimer);
    for(int i = 0; i < MAX_LED_NUM; i++){
        GpioSetLevel(s_led_list[i].id, false);
    }
    GpioSetLevel(s_led_list[idx].id, true);

    if(++idx >= MAX_LED_NUM){
        idx = 0;
    }
    osiTimerStart(ledTimer, 500);
}

void framework_led_factory_blink(void)
{
    ledTimer = osiTimerCreate(prv_Led_factory_Handle, NULL);
    if(ledTimer == NULL){
        return;
    }
    osiTimerStart(ledTimer, 500);
}