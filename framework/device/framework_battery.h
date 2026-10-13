/**
 * @file framework_battery.h
 * @brief Framework Battery Management Module Header
 * @details Defines battery management interfaces including battery level query, charging state detection, etc.
 */

#ifndef _FRAMEWORK_BATTERY_H_
#define _FRAMEWORK_BATTERY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Battery voltage-capacity table configuration
 * @details Used by customer layer to provide battery characteristics
 */
typedef struct {
    const uint16_t (*discharge_table)[2];  // Discharge voltage-capacity table
    uint8_t discharge_table_size;          // Number of entries in discharge table
    const uint16_t (*charge_table)[2];     // Charge voltage-capacity table
    uint8_t charge_table_size;             // Number of entries in charge table
} battery_config_t;


typedef void (*device_sleep_callback_t)(uint8_t event, uint8_t enable);
/**
 * @brief Register battery voltage-capacity configuration tables
 * @details Must be called before framework_battery_init()
 * @param config Pointer to battery configuration structure
 * @return 0=success, -1=invalid parameter
 */
int framework_battery_register_config(const battery_config_t *config);

/**
 * @brief Initialize battery management module
 * @details Initializes ADC sampling, starts USB detection task, initializes voltage array
 * @return 0=success, -1=failure
 */
int framework_battery_init(void);

/**
 * @brief Configure low-power notification intervals
 * @param low_ms  Interval (ms) for more urgent low-power reminders
 * @param high_ms Interval (ms) for less urgent low-power reminders
 * @return 0 on success, -1 on invalid parameters
 */
int framework_battery_set_lowpower_intervals(uint32_t low_ms, uint32_t high_ms);

/**
 * @brief Get battery capacity percentage
 * @details Calculate battery percentage (0-100%) based on current voltage and charging/discharging state
 * @return Battery percentage (0-100), 0=read failed or battery empty
 */
uint8_t framework_battery_get_percent(void);

/**
 * @brief Get battery voltage
 * @details Returns battery voltage after sliding average filtering (unit: mV)
 * @return Battery voltage (mV), 0=read failed
 */
uint32_t framework_battery_get_voltage(void);

/**
 * @brief Query if charging
 * @details Detect charging state (VBUS present + CHARGER_DETECT_PIN=0)
 * @return true=charging, false=not charging
 */
bool framework_battery_is_charging(void);

/**
 * @brief Query if USB is plugged in
 * @details Detect if VBUS is present
 * @return true=USB plugged in, false=USB not plugged in
 */
bool framework_battery_is_usb_plugin(void);

/**
 * @brief Query if battery is fully charged
 * @details Detect charging complete state (CHARGER_STDBY_PIN=0)
 * @return true=battery fully charged, false=battery not full
 */
bool framework_battery_is_full(void);

/**
 * @brief Enable or disable device sleep mode for specific event
 * @details Control the sleep mode state of the device based on given event
 * @param event The event type that triggers sleep mode
 * @param enable true to enable sleep mode, false to disable sleep mode
 */
void framework_device_enable_sleep(uint8_t event, uint8_t enable);

/**
 * @brief Register callback function for device sleep event
 * @details Register a callback function that will be called when device sleep state changes
 * @param callback Pointer to the callback function to be registered
 * @return 0 on success, negative value on error
 */
int framework_register_sleep_callback(device_sleep_callback_t callback);

/**
 * @brief Get battery voltage
 * @details Returns battery voltage after sliding average filtering (unit: mV)
 * @return Battery voltage (mV), 0=read failed
 */
uint32_t framework_factory_battery_voltage(void);
#ifdef __cplusplus
}
#endif

#endif /* _FRAMEWORK_BATTERY_H_ */
