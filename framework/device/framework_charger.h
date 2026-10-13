/**
 * @file framework_charger.h
 * @brief Charger detection and fake power-on handling
 */

#ifndef FRAMEWORK_CHARGER_H
#define FRAMEWORK_CHARGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power-on reason enumeration
 */
typedef enum {
    POWERON_REASON_UNKNOWN = 0,     /**< Unknown reason */
    POWERON_REASON_KEY = 1,         /**< Power key pressed */
    POWERON_REASON_CHARGER = 2,     /**< Charger/USB plugged in */
    POWERON_REASON_ALARM = 3,       /**< RTC alarm */
    POWERON_REASON_RESET = 4,       /**< Reset */
} poweron_reason_t;

/**
 * @brief Charger fake power-on result
 */
typedef enum {
    CHARGER_BOOT_NORMAL = 0,        /**< Normal boot - enter application */
    CHARGER_BOOT_FAKE = 1,          /**< Fake boot - stay in charging mode */
    CHARGER_BOOT_POWEROFF = 2,      /**< Should power off */
} charger_boot_result_t;

/**
 * @brief Initialize charger detection
 * @return 0 on success, negative on error
 */
int framework_charger_init(void);

/**
 * @brief Get power-on reason
 * @return poweron_reason_t
 */
poweron_reason_t framework_charger_get_poweron_reason(void);

/**
 * @brief Handle charger boot logic
 * @details Determines if system should enter fake boot mode when USB is plugged
 * @return charger_boot_result_t
 */
charger_boot_result_t framework_charger_handle_boot(void);

/**
 * @brief Check if USB/charger is currently connected
 * @return true if connected, false otherwise
 */
bool framework_charger_is_connected(void);

/**
 * @brief Enter fake power-on mode (charging only, no app)
 * @details Shows charging animation, monitors USB state and power key
 * @return 0 if user pressed key to enter app, negative if should power off
 */
int framework_charger_fake_poweron(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_CHARGER_H */
