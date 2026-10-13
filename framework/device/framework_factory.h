/**
 * @file framework_factory.h
 * @brief Factory mode detection and test framework
 */

#ifndef FRAMEWORK_FACTORY_H
#define FRAMEWORK_FACTORY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Factory mode detection result
 */
typedef enum {
    FACTORY_MODE_NONE = 0,      /**< Normal mode */
    FACTORY_MODE_DETECTED = 1,  /**< Factory mode detected */
} factory_mode_result_t;

/**
 * @brief Factory mode callback function type
 * Called when entering factory mode
 */
typedef void (*factory_mode_callback_t)(void);

/**
 * @brief Initialize factory mode detection
 * @return 0 on success, negative on error
 */
int framework_factory_init(void);

/**
 * @brief Check if should enter factory mode
 * @details Checks specific conditions (e.g., specific key combination)
 * @return factory_mode_result_t
 */
factory_mode_result_t framework_factory_check_mode(void);

/**
 * @brief Enter factory mode
 * @details Initializes factory test environment
 * @param callback Optional callback when entering factory mode
 * @return 0 on success, negative on error
 */
int framework_factory_enter(factory_mode_callback_t callback);

/**
 * @brief Check if currently in factory mode
 * @return true if in factory mode, false otherwise
 */
bool framework_factory_is_active(void);

/**
 * @brief Factory mode message processing
 * @param msg_id Message ID
 * @param param Message parameter
 */
void framework_factory_process_message(uint32_t msg_id, void *param);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_FACTORY_H */
