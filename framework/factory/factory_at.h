/**
 * @file factory_at.h
 * @brief Factory mode AT command framework
 */

#ifndef FACTORY_AT_H
#define FACTORY_AT_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AT command end-of-line character */
#define AT_CMD_EOF  '\r'

/* Default data receive timeout (30 seconds) */
#define RECV_DATA_TIMEOUT_DEFAULT  (30 * 1000)

/**
 * @brief AT command processing state
 */
typedef enum {
    AT_STATE_IDLE,              /**< Idle, waiting for command */
    AT_STATE_CMD_RECVING,       /**< Receiving command */
    AT_STATE_CMD_PROCESS,       /**< Processing command */
    AT_STATE_DATA_RECVING,      /**< Receiving data */
    AT_STATE_DATA_WAIT_SEND,    /**< Waiting to send data */
    AT_STATE_DATA_SENDING,      /**< Sending data */
} at_state_t;

/**
 * @brief AT command return type
 */
typedef enum {
    AT_RET_OK = 0,              /**< Command success */
    AT_RET_ERROR = -1,          /**< Command error */
    AT_RET_WAIT_DATA = -2,      /**< Waiting for data */
} at_ret_t;

/**
 * @brief AT command function structure
 */
typedef struct {
    const char *cmd_name;                               /**< Command name (e.g., "AT+TEST") */
    at_ret_t (*test_cmd)(void);                         /**< Test command handler (AT+CMD=?) */
    at_ret_t (*query_cmd)(void);                        /**< Query command handler (AT+CMD?) */
    at_ret_t (*setup_cmd)(uint8_t id, int8_t *param);   /**< Setup command handler (AT+CMD=...) */
    at_ret_t (*exe_cmd)(void);                          /**< Execute command handler (AT+CMD) */
} at_cmd_function_t;

/**
 * @brief Data receive callback function type
 * @param data Received data buffer
 * @param len Data length
 * @return at_ret_t
 */
typedef at_ret_t (*at_data_recv_handler_t)(const int8_t *data, uint32_t len);

/**
 * @brief Extra command handler for unregistered AT commands
 * @param data Command data
 * @param len Command length
 * @return at_ret_t
 */
typedef at_ret_t (*at_extra_cmd_handler_t)(const int8_t *data, uint32_t len);

/**
 * @brief Initialize AT command system
 * @param cmd_list Array of AT command functions
 * @param cmd_count Number of commands in array
 * @return 0 on success, negative on error
 */
int factory_at_init(at_cmd_function_t *cmd_list, uint32_t cmd_count);

/**
 * @brief Process received AT command data
 * @param data Received data buffer
 * @param len Data length
 */
void factory_at_process(const int8_t *data, uint32_t len);

/**
 * @brief Wait for data reception
 * @param len Expected data length
 * @param callback Callback when data received
 * @param timeout_sec Timeout in seconds (0 = default 30s)
 * @return 0 on success, negative on error
 */
int factory_at_wait_data(uint32_t len, at_data_recv_handler_t callback, uint32_t timeout_sec);

/**
 * @brief Send AT command response
 * @param fmt Format string (printf-style)
 * @param ... Variable arguments
 * @return 0 on success, negative on error
 */
int factory_at_response(const char *fmt, ...);

/**
 * @brief Send binary data
 * @param data Data buffer
 * @param len Data length
 */
void factory_at_send_data(const int8_t *data, uint32_t len);

/**
 * @brief Register extra command handler for unregistered commands
 * @param handler Extra command handler function
 */
void factory_at_register_extra_handler(at_extra_cmd_handler_t handler);

/**
 * @brief User-specific AT initialization (must be implemented by user)
 * @return 0 on success, negative on error
 */
int factory_at_init_user(void);

/**
 * @brief User-specific AT response output (must be implemented by user)
 * @param data Data to send
 * @param len Data length
 * @return 0 on success, negative on error
 */
int factory_at_response_user(const int8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_AT_H */
