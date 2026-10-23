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
#include <stddef.h>

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
    char serial_number[32];           /**< Device serial number (serial_no) */
    char transaction_id[64];          /**< Transaction ID (TxnId) */
    char status[16];                  /**< SoundPlayStatus: "1" or "0" or "SUCCESS"/"FAILURE" */
    char amount[16];                  /**< Amount string (e.g. "16.32") */
    char language[8];                 /**< Language (e.g. "EN") */
    char reason[32];                  /**< Reason ("" on success, error msg on fail) */
    char sound_played_datetime[32];   /**< Datetime "YYYY-MM-DD HH:MM:SS" */
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

/**
 * @brief Get formatted date-time string ("YYYY-MM-DD HH:MM:SS")
 * @param buf Output buffer
 * @param buf_len Size of buffer
 */
void customer_dux_get_datetime_str(char *buf, size_t buf_len);

/**
 * @brief Get network quality string ("Good", "Normal", "Bad")
 * @return String constant
 */
const char* customer_dux_get_network_quality_str(void);

/**
 * @brief Publish Device ON/OFF event to SBHealthStatus topic
 * @param is_on 1 for Device ON, 0 for Device OFF (shutdown)
 * @return 0 on success, negative on error
 */
int customer_dux_publish_device_power(uint8_t is_on);

/**
 * @brief Publish Charging/Discharging event to SBHealthStatus topic
 * @param is_charging 1 for connected, 0 for disconnected
 * @return 0 on success, negative on error
 */
int customer_dux_publish_charging_status(uint8_t is_charging);

/**
 * @brief Publish 15-minute Periodic Health Status to SBHealthStatus topic
 * @return 0 on success, negative on error
 */
int customer_dux_publish_health_status(void);

/**
 * @brief Check if both MQTT and NTP are ready, and publish initial Device ON event
 */
void customer_dux_check_and_publish_device_on(void);

/**
 * @brief Get whether NTP sync has been completed
 * @return 1 if synced, 0 if not
 */
uint8_t customer_get_report_ntp_sync(void);

/**
 * @brief Set NTP sync state
 * @param status 1 for synced, 0 for not synced
 * @return 0 on success
 */
int customer_set_report_ntp_sync(uint8_t status);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_STATUS_REPORT_H_ */
