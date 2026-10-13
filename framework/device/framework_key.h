/**
 * @file framework_key.h
 * @brief Framework按键管理模块头文件
 * @details 定义按键ID、接口和Power Key回调类型
 */

#ifndef _FRAMEWORK_KEY_H_
#define _FRAMEWORK_KEY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 按键ID定义
 */
typedef enum {
    KEY_ID_VOLUME_UP = 0,    /**< 音量加键 */
    KEY_ID_VOLUME_DOWN = 1,  /**< 音量减键 */
    KEY_ID_FUNCTION = 2,     /**< 功能键 */
    KEY_ID_POWER = 3,        /**< 电源键 */
    KEY_ID_MAX
} key_id_e;

/**
 * @brief Power Key事件类型
 */
typedef enum {
    POWKEY_EVENT_PRESS = 0,      /**< 按键按下 */
    POWKEY_EVENT_RELEASE = 1,    /**< 按键释放 */
    POWKEY_EVENT_LONG_PRESS = 2  /**< 按键长按 */
} powkey_event_e;

/**
 * @brief Power Key回调函数类型
 * @param event Power Key事件
 * @param param 用户参数
 */
typedef void (*power_key_callback_t)(powkey_event_e event, void *param);

/**
 * @brief 初始化按键管理模块
 * @return 0=成功, -1=失败
 */
int framework_key_init(void);

/**
 * @brief 使能按键检测
 * @param key_id 按键ID
 * @param enable 1=使能, 0=禁用
 * @return 0=成功, -1=失败
 */
int framework_key_enable(key_id_e key_id, uint8_t enable);

/**
 * @brief 注册Power Key回调函数
 * @param callback 回调函数指针
 * @param param 用户参数
 * @return 0=成功, -1=失败
 */
int framework_key_register_pwk_callback(power_key_callback_t callback, void *param);

/**
 * @brief 设置Power Key长按时间
 * @param time_ms 长按时间(ms)
 * @return 0=成功, -1=失败
 */
int framework_key_set_pwk_longpress_time(uint16_t time_ms);

/**
 * @brief 设置普通按键长按时间
 * @param time_ms 长按时间(ms)
 * @return 0=成功, -1=失败
 */
int framework_key_set_longpress_time(uint16_t time_ms);

/**
 * @brief 更新VBUS状态
 * @param state VBUS状态 (1=连接, 0=断开)
 */
void framework_key_update_vbus_state(uint8_t state);

/**
 * @brief 获取开机原因
 * @return 0=未知, 1=Power Key, 2=USB
 */
uint8_t framework_key_get_poweron_reason(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRAMEWORK_KEY_H_ */
