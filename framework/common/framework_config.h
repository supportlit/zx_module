/**
 * @file framework_config.h
 * @brief Framework通用硬件配置
 *
 * 说明:
 * - 定义所有通用硬件配置参数 (从app_config.h合并而来)
 * - 客户可在customer_config.h中覆盖这些配置
 * - 所有宏定义使用#ifndef保护,支持客户自定义
 */

#ifndef __FRAMEWORK_CONFIG_H__
#define __FRAMEWORK_CONFIG_H__

#include "drv_def.h"

/* Customer config now primarily arrives via compile definitions; header include is legacy. */
#ifdef CUSTOMER_CONFIG_HEADER
#include CUSTOMER_CONFIG_HEADER
#elif defined(__has_include)
#  if __has_include("customer_config.h")
#    include "customer_config.h"
#  endif
#else
#include "customer_config.h"
#endif

/* ============================================================================ 
 * 硬件模块选择 (参照app_config.h)
 * ========================================================================== */ 

// ZX800SG_SA 或 ZX800VG_CA 模块由 core_config.cmake 定义
// MODULE_ZX800SG_SA 或 MODULE_ZX800VG_CA

/* ============================================================================ 
 * GPIO引脚定义 (参照app_config.h)
 * ========================================================================== */ 

// 按键GPIO引脚
#ifndef KEY_DOWN_PIN
#define KEY_DOWN_PIN        86  // 音量减
#endif

#ifndef KEY_UP_PIN
#define KEY_UP_PIN          37  // 音量加
#endif

#ifndef KEY_FUN_PIN
#define KEY_FUN_PIN         10  // 功能键
#endif

// 按键PINMUX配置
#ifndef KEY_DOWN_PINMUX
#define KEY_DOWN_PINMUX     GPIO_VCXO_OUT
#endif

#ifndef KEY_UP_PINMUX
#define KEY_UP_PINMUX       GPIO_37
#endif

#ifndef KEY_FUN_PINMUX
#define KEY_FUN_PINMUX      GPIO_10
#endif

// 按键功能配置
#ifndef KEY_DOWN_FUN
#define KEY_DOWN_FUN        MFP_AF1
#endif

#ifndef KEY_UP_FUN
#define KEY_UP_FUN          MFP_AF0
#endif

#ifndef KEY_FUN_FUN
#define KEY_FUN_FUN         MFP_AF0
#endif

// LED GPIO引脚
#ifndef LED_RED_PIN
#define LED_RED_PIN         85  // 红色LED
#endif

#ifndef LED_GREEN_PIN
#define LED_GREEN_PIN       2   // 绿色LED
#endif

#ifndef LED_BLUE_PIN
#define LED_BLUE_PIN        14  // 蓝色LED
#endif

// LED PINMUX配置
#ifndef LED_RED_PINMUX
#define LED_RED_PINMUX      GPIO_VCXO_REQ
#endif

#ifndef LED_GREEN_PINMUX
#define LED_GREEN_PINMUX    GPIO_02
#endif

#ifndef LED_BLUE_PINMUX
#define LED_BLUE_PINMUX     GPIO_14
#endif

// LED功能配置
#ifndef LED_RED_FUN
#define LED_RED_FUN         MFP_AF1
#endif

#ifndef LED_GREEN_FUN
#define LED_GREEN_FUN       MFP_AF0
#endif

#ifndef LED_BLUE_FUN
#define LED_BLUE_FUN        MFP_AF0
#endif

// 充电相关GPIO引脚
#ifndef CHARGER_DETECT_PIN
#define CHARGER_DETECT_PIN  1   // 充电检测引脚
#endif

#ifndef CHARGER_STDBY_PIN
#define CHARGER_STDBY_PIN   0   // 充电待机引脚
#endif

// 充电PINMUX配置
#ifndef CHARGER_DETECT_PINMUX
#define CHARGER_DETECT_PINMUX   GPIO_01
#endif

#ifndef CHARGER_STDBY_PINMUX
#define CHARGER_STDBY_PINMUX    GPIO_00
#endif

// 充电功能配置
#ifndef CHARGER_DETECT_PINFUN
#define CHARGER_DETECT_PINFUN   MFP_AF0
#endif

#ifndef CHARGER_STDBY_PINFUN
#define CHARGER_STDBY_PINFUN    MFP_AF0
#endif

// 功放GPIO引脚
#ifndef GPIO_PA
#define GPIO_PA             11  // 功放使能
#endif

#ifndef PA_PINMUX
#define PA_PINMUX           GPIO_11
#endif

#ifndef PA_PINFUN
#define PA_PINFUN           MFP_AF0
#endif

// 编解码器GPIO引脚
#ifndef GPIO_CODEC
#define GPIO_CODEC          68  // 编解码器使能
#endif

#ifndef CODEC_PINMUX
#define CODEC_PINMUX        GPIO_68
#endif

#ifndef CODEC_PINFUN
#define CODEC_PINFUN        MFP_AF0
#endif

// SIM卡检测GPIO引脚
#ifndef SIM_CD_PIN
#define SIM_CD_PIN          35  // SIM卡检测
#endif

#ifndef SIM_CD_PINMUX
#define SIM_CD_PINMUX       GPIO_35
#endif

#ifndef SIM_CD_PINFUN
#define SIM_CD_PINFUN       MFP_AF0
#endif

#ifndef LCD_SDA_PINMUX
#define LCD_SDA_PINMUX    (GPIO_36)
#endif

#ifndef LCD_SCL_PINMUX
#define LCD_SCL_PINMUX    (GPIO_33)
#endif

#ifndef LCD_RST_PINMUX
#define LCD_RST_PINMUX    (GPIO_56)
#endif

#ifndef KEY_LCD_FUN
#define KEY_LCD_FUN        MFP_AF0
#endif

#ifndef LCD_SDA_PIN
#define LCD_SDA_PIN    (36)
#endif

#ifndef LCD_SCL_PIN
#define LCD_SCL_PIN    (33)
#endif

#ifndef LCD_RST_PIN
#define LCD_RST_PIN    (56)
#endif

/* ============================================================================ 
 * 音量参数 (参照app_config.h)
 * ========================================================================== */ 

#ifndef VOL_MIN
#define VOL_MIN             1   // 最小音量
#endif

#ifndef VOL_FULL
#define VOL_FULL            7   // 满音量
#endif

#ifndef VOL_STEP
#define VOL_STEP            1   // 音量步进
#endif

#ifndef VOL_MAX
#define VOL_MAX             8   // 最大音量
#endif

#ifndef DEFAULT_VOL
#define DEFAULT_VOL         VOL_MIN  // 默认音量
#endif

/* ============================================================================ 
 * LED参数 (参照app_config.h)
 * ========================================================================== */ 

#ifndef MAX_LED_NUM
#define MAX_LED_NUM         3   // LED数量
#endif

/* ============================================================================ 
 * 按键参数 (参照app_config.h)
 * ========================================================================== */ 

#ifndef KEY_CNT_MAX
#define KEY_CNT_MAX         3   // 按键数量
#endif

/* ============================================================================ 
 * 按键时序参数 (参照app_key.c)
 * ========================================================================== */ 

// 按键防抖时间 (ms)
#ifndef KEY_ANTI_SHAKE_MS
#define KEY_ANTI_SHAKE_MS           50
#endif

// 按键长按时间 (ms)
#ifndef KEY_LONG_PRESS_MS
#define KEY_LONG_PRESS_MS           2000
#endif

// Power Key长按时间 (ms)
#ifndef PWK_LONG_PRESS_MS
#define PWK_LONG_PRESS_MS           3000
#endif

// Power Key防误触时长 (ms)
#ifndef PWK_VBUS_FILTER_MS
#define PWK_VBUS_FILTER_MS          20
#endif

/* ============================================================================ 
 * 电池参数 (参照app_charger.c)
 * ========================================================================== */ 

// USB插拔检测间隔 (ms)
#ifndef USB_DETECT_INTERVAL_MS
#define USB_DETECT_INTERVAL_MS      500
#endif

// USB检测间隔 (ms) - 兼容旧代码
#ifndef USB_DETECT_INTERVAL
#define USB_DETECT_INTERVAL         USB_DETECT_INTERVAL_MS
#endif

// 电压更新周期 (秒)
#ifndef BAT_VOLTAGE_UPDATE_SEC
#define BAT_VOLTAGE_UPDATE_SEC      15
#endif

// ADC读取计数 (15秒 / 500ms = 30)
#ifndef READ_ADC_CNT
#define READ_ADC_CNT                (BAT_VOLTAGE_UPDATE_SEC * 1000 / USB_DETECT_INTERVAL_MS)
#endif

// 电压滑动平均数组大小
#ifndef BAT_DETECT_ARRAY_SIZE
#define BAT_DETECT_ARRAY_SIZE       20
#endif

// 放电电压表条目数
#ifndef BAT_DISCHARGE_VOLTAGE_STEP
#define BAT_DISCHARGE_VOLTAGE_STEP  11
#endif

// 充电电压表条目数
#ifndef BAT_CHARGE_VOLTAGE_STEP
#define BAT_CHARGE_VOLTAGE_STEP     11
#endif

// ADC采样次数
#ifndef BAT_ADC_SAMPLE_COUNT
#define BAT_ADC_SAMPLE_COUNT        10
#endif

// ADC采样次数 - 兼容旧代码
#ifndef ADC_SAMPLE_CNT
#define ADC_SAMPLE_CNT              BAT_ADC_SAMPLE_COUNT
#endif

// 电量上报来源：1=BMIC，0=ADC换算
#ifndef BATTERY_PERCENT_SOURCE_BMIC
#define BATTERY_PERCENT_SOURCE_BMIC 0
#endif

// 电量上报周期 (ms)
#ifndef BATTERY_PERCENT_REPORT_MS
#define BATTERY_PERCENT_REPORT_MS   3000
#endif

// 充电满电压 (mV)
#ifndef CHARGE_FULL_VOLT_MV
#define CHARGE_FULL_VOLT_MV         4150
#endif

// 低电压阈值 (mV)
#ifndef LOW_POWER_THRESHOLD_MV
#define LOW_POWER_THRESHOLD_MV      3650
#endif

// 低电第一档提醒间隔 (分钟) - 3600-3574mV
#ifndef LOW_POWER_REMIND_1_MIN
#define LOW_POWER_REMIND_1_MIN      30
#endif

// 低电第一档提醒间隔 (ms) - 兼容旧代码
#ifndef LOWER_POWER_INTERVAL
#define LOWER_POWER_INTERVAL        (LOW_POWER_REMIND_1_MIN * 60 * 1000)
#endif

// 低电第二档提醒间隔 (分钟) - 3574-3501mV
#ifndef LOW_POWER_REMIND_2_MIN
#define LOW_POWER_REMIND_2_MIN      10
#endif

// 低电第二档提醒间隔 (ms) - 兼容旧代码
#ifndef LOWEST_POWER_INTERVAL
#define LOWEST_POWER_INTERVAL       (LOW_POWER_REMIND_2_MIN * 60 * 1000)
#endif

// 极低电压关机阈值 (mV)
#ifndef CRITICAL_LOW_VOLT_MV
#define CRITICAL_LOW_VOLT_MV        3501
#endif

#ifndef FRAMEWORK_POWEROFF_CHARGER_ENABLE
#define FRAMEWORK_POWEROFF_CHARGER_ENABLE 0
#endif

/* ============================================================================ 
 * LED时序参数
 * ========================================================================== */ 

// LED快闪周期 (ms)
#ifndef LED_FAST_BLINK_MS
#define LED_FAST_BLINK_MS           500
#endif

// LED慢闪周期 (ms)
#ifndef LED_SLOW_BLINK_MS
#define LED_SLOW_BLINK_MS           1000
#endif

/* ============================================================================ 
 * 任务优先级 (参照app相关文件)
 * ========================================================================== */ 

#ifndef KEY_TASK_PRIORITY
#define KEY_TASK_PRIORITY               100
#endif

#ifndef AUDIO_TASK_PRIORITY
#define AUDIO_TASK_PRIORITY             101
#endif

#ifndef USB_PLUG_DETECT_TASK_PRIORITY
#define USB_PLUG_DETECT_TASK_PRIORITY   120
#endif

// USB检测任务优先级 - 兼容旧代码
#ifndef USB_DETECT_TASK_PRIORITY
#define USB_DETECT_TASK_PRIORITY        115
#endif

#ifndef LED_TASK_PRIORITY
#define LED_TASK_PRIORITY               90
#endif

/* ============================================================================ 
 * 任务栈大小 (字节)
 * ========================================================================== */ 

#ifndef KEY_TASK_STACK_SIZE
#define KEY_TASK_STACK_SIZE             2048
#endif

#ifndef AUDIO_TASK_STACK_SIZE
#define AUDIO_TASK_STACK_SIZE           8192
#endif

#ifndef USB_DETECT_TASK_STACK_SIZE
#define USB_DETECT_TASK_STACK_SIZE      2048
#endif

#ifndef LED_TASK_STACK_SIZE
#define LED_TASK_STACK_SIZE             2048
#endif

/* ============================================================================ 
 * 消息队列大小
 * ========================================================================== */ 

#ifndef MSG_QUEUE_SIZE
#define MSG_QUEUE_SIZE                  60
#endif

/* ============================================================================ 
 * 文件路径配置 (参照app_config.h)
 * ========================================================================== */ 

#ifndef DEVICE_INFO_PATH
#define DEVICE_INFO_PATH    "devinfo.cfg"
#endif

#ifndef DEVICE_SN_PATH
#define DEVICE_SN_PATH      "sn.cfg"
#endif

#ifndef DEVICE_WIFI_PATH
#define DEVICE_WIFI_PATH    "wifi.cfg"
#endif

#ifndef RESOURCE_INFO_PATH
#define RESOURCE_INFO_PATH  "D:/resource_list.json"
#endif

/* ============================================================================ 
 * 音频音量配置
 * ========================================================================== */
#ifndef VOL_MIN
#define VOL_MIN       (1)
#endif

#ifndef VOL_FULL
#define VOL_FULL      (7)
#endif

#ifndef VOL_STEP
#define VOL_STEP      (1)
#endif

#ifndef VOL_MAX
#define VOL_MAX       (8)
#endif

#endif /* __FRAMEWORK_CONFIG_H__ */
