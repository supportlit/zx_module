/**
 * @file framework_led.h
 * @brief Framework LED管理模块头文件
 * @details 定义LED状态管理接口和客户回调机制
 */

#ifndef _FRAMEWORK_LED_H_
#define _FRAMEWORK_LED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief LED状态定义 (位掩码)
 * @details 优先级: 异常状态(bit0-2) > 充电状态(bit3-4) > 网络状态(bit5-6)
 */
typedef enum {
    LED_STATE_EXCEPTION_SERVER  = (0x1 << 0),  /**< bit0: 服务器异常 (红灯快闪500ms) */
    LED_STATE_EXCEPTION_NET     = (0x1 << 1),  /**< bit1: 网络异常 (红灯慢闪1000ms) */
    LED_STATE_EXCEPTION_POWER   = (0x1 << 2),  /**< bit2: 电源异常 (红灯常亮) */
    LED_STATE_CHARGING          = (0x1 << 3),  /**< bit3: 充电中 (蓝灯慢闪1000ms) */
    LED_STATE_CHARGE_FULL       = (0x1 << 4),  /**< bit4: 充电完成 (蓝灯常亮) */
    LED_STATE_NET_CONNECTING    = (0x1 << 5),  /**< bit5: 网络连接中 (绿灯快闪500ms) */
    LED_STATE_NET_CONNECTED     = (0x1 << 6),  /**< bit6: 网络已连接 (绿灯常亮) */
    LED_STATE_ALL_OFF          = (0x1 << 31),  /**< bit7: 所有LED关闭 */
} led_state_e;

/**
 * @brief LED状态处理回调函数类型
 * @details 客户可注册此回调来自定义LED行为
 * @param state 当前要显示的LED状态(只包含一个状态位,按优先级选中)
 * @param param 用户参数
 * @return 0=客户已处理(不执行默认GPIO控制), -1=未处理(使用Framework默认实现)
 */
typedef int (*led_state_handler_t)(uint32_t state, void *param);

/**
 * @brief 初始化LED管理模块
 * @details 初始化GPIO、创建LED定时器、设置初始状态
 * @return 0=成功, -1=失败
 */
int framework_led_init(void);

/**
 * @brief 设置LED状态 (OR操作)
 * @details 将指定状态位置1,触发LED更新(高优先级状态显示)
 * @param state LED状态位掩码(可OR多个状态)
 * @return 0=成功, -1=失败
 */
int framework_led_set_state(uint32_t state);

/**
 * @brief 清除LED状态 (AND NOT操作)
 * @details 将指定状态位清0,触发LED更新
 * @param state LED状态位掩码(可OR多个状态)
 * @return 0=成功, -1=失败
 */
int framework_led_clear_state(uint32_t state);

/**
 * @brief 清除所有LED状态
 * @details 清除所有状态位,关闭所有LED
 * @return 0=成功, -1=失败
 */
int framework_led_clear_all(void);

/**
 * @brief 注册LED状态处理回调
 * @details 客户通过此函数注册自定义LED处理逻辑
 * @param handler 回调函数指针(NULL=取消注册)
 * @param param 用户参数(传递给回调函数)
 * @return 0=成功, -1=失败
 */
int framework_led_register_state_handler(led_state_handler_t handler, void *param);


/**
 * @brief 设置LED闪烁状态
 * @details 设置指定LED引脚是否闪烁
 * @param pin LED引脚号
 * @param interval_ms 闪烁间隔毫秒数(0=不闪烁)
 */
void framework_led_set_blink(uint32_t pin, uint32_t interval_ms);

/**
 * @brief 工厂模式
 * @details 工厂模式
 */
void framework_led_factory_blink(void);


void framework_led_all_off(void);
#ifdef __cplusplus
}
#endif

#endif /* _FRAMEWORK_LED_H_ */
