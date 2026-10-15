/**
 * @file customer_status_report.h
 * @brief template status reporting module
 */

#ifndef _CUSTOMER_STATUS_REPORT_H_
#define _CUSTOMER_STATUS_REPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Connection type enumeration
 */
typedef enum {
    CONNECTION_TYPE_4G = 0,
    CONNECTION_TYPE_WIFI = 1,
    CONNECTION_TYPE_UNKNOWN = 2
} connection_type_e;

/**
 * @brief Device status enumeration
 */
typedef enum {
    DEVICE_STATUS_ONLINE = 0,
    DEVICE_STATUS_OFFLINE = 1,
    DEVICE_STATUS_IDLE = 2
} device_status_e;

/**
 * @brief Charging status enumeration
 */
typedef enum {
    CHARGING_STATUS_PLUG = 0,
    CHARGING_STATUS_UNPLUG = 1
} charging_status_e;

/**
 * @brief Soundbox status structure
 */
typedef struct {
    char serial_number[32];           /**< Device serial number */
    uint8_t battery;                  /**< Battery level (0-100) */
    char signal[16];                  /**< Signal strength (strong/medium/weak) */
    device_status_e status;           /**< Device status */
    char firm_ver[64];                /**< Firmware version */
    connection_type_e connection_type;/**< Connection type */
} soundbox_status_t;

/**
 * @brief Charging status structure
 */
typedef struct {
    char serial_number[32];           /**< Device serial number */
    uint8_t battery;                  /**< Battery level (0-100) */
    charging_status_e status;         /**< Charging status */
} charging_status_t;

/**
 * @brief Device coordinate structure
 */
typedef struct {
    char serial_number[32];           /**< Device serial number */
    double lat;                       /**< Latitude */
    double longitude;                 /**< Longitude (use 'long' as field name) */
} device_coordinate_t;

/**
 * @brief Payment callback structure
 */
typedef struct {
    char serial_number[32];           /**< Device serial number */
    char transaction_id[64];          /**< Transaction ID */
    char status[16];                  /**< Status: SUCCESS/FAILURE */
} payment_callback_t;

/**
 * @brief Initialize status reporting module
 * @return 0 on success, -1 on error
 */
int customer_status_report_init(void);

/**
 * @brief Deinitialize status reporting module
 * @return 0 on success
 */
int customer_status_report_deinit(void);

/**
 * @brief Start periodic status reporting
 * @param interval_sec Reporting interval in seconds (0 to stop)
 * @return 0 on success, -1 on error
 */
int customer_status_report_start(uint32_t interval_sec);

/**
 * @brief Stop periodic status reporting
 * @return 0 on success
 */
int customer_status_report_stop(void);

/**
 * @brief Publish soundbox status immediately
 * @param status Pointer to soundbox status structure
 * @return 0 on success, -1 on error
 */
int customer_status_report_soundbox(const soundbox_status_t *status);

/**
 * @brief Publish charging status
 * @param status Pointer to charging status structure
 * @return 0 on success, -1 on error
 */
int customer_status_report_charging(const charging_status_t *status);

/**
 * @brief Publish device coordinate
 * @param coord Pointer to device coordinate structure
 * @return 0 on success, -1 on error
 */
int customer_status_report_coordinate(const device_coordinate_t *coord);

/**
 * @brief Publish payment callback
 * @param callback Pointer to payment callback structure
 * @return 0 on success, -1 on error
 */
int customer_status_report_payment(const payment_callback_t *callback);

/**
 * @brief Send TMS HTTPS heartbeat immediately
 * @return 0 on success, negative error code on failure
 */
int customer_send_tms_heartbeat(void);

/**
 * @brief Start periodic TMS HTTPS heartbeat
 * @param interval_sec Heartbeat interval in seconds (0 to use TMS_HEARTBEAT_INTERVAL_SEC)
 * @return 0 on success, negative error code on failure
 */
int customer_tms_heartbeat_start(uint32_t interval_sec);

/**
 * @brief Stop periodic TMS HTTPS heartbeat
 * @return 0 on success
 */
int customer_tms_heartbeat_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_STATUS_REPORT_H_ */
