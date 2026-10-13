/**
 * @file framework_msg.h
 * @brief Framework Message Management Module Header
 * @details Defines message structures, message IDs, status enums and message processing interfaces
 */

#ifndef _FRAMEWORK_MSG_H_
#define _FRAMEWORK_MSG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Message structure
 */
typedef struct {
    uint32_t id;      /**< Message ID */
    uint32_t param1;  /**< Parameter 1 */
    uint32_t param2;  /**< Parameter 2 */
    uint32_t param3;  /**< Parameter 3 */
} framework_msg_t;

/**
 * @brief Key Message ID Definitions (0x1001-)
 */
#define MSG_KEY_GROUP        0x1000  /**< Grouped Key Message Base */
#define MSG_IS_KEY_GROUP(msg_id)  ((msg_id & MSG_KEY_GROUP) == MSG_KEY_GROUP)
#define MSG_KEY_POWER           0x1001  /**< Power Key */
#define MSG_KEY_FUNCTION        0x1002  /**< Function Key */
#define MSG_KEY_VOL_UP          0x1003  /**< Volume Up */
#define MSG_KEY_VOL_DOWN        0x1004  /**< Volume Down */

/**
 * @brief Charge Message ID Definitions (0x2001-)
 */
#define MSG_CHARGE_GROUP        0x2000  /**< Grouped Charge Message Base */
#define MSG_IS_CHARGE_GROUP(msg_id)  ((msg_id & MSG_CHARGE_GROUP) == MSG_CHARGE_GROUP)
#define MSG_CHARGE_PLUGIN       0x2001  /**< Charger Plugged In */
#define MSG_CHARGE_PLUGOUT      0x2002  /**< Charger Unplugged */
#define MSG_CHARGE_FULL         0x2003  /**< Charging Complete */
#define MSG_CHARGE_LOW_POWER    0x2004  /**< Low Battery Warning */
#define MSG_CHARGE_PERCENT_CHANGE 0x2005  /**< Battery Percentage Changed */

/**
 * @brief Network Message ID Definitions (0x4001-)
 */
#define MSG_NET_GROUP         0x4000  /**< Grouped Network Message Base */
#define MSG_IS_NET_GROUP(msg_id)  ((msg_id & MSG_NET_GROUP) == MSG_NET_GROUP)
#define MSG_NET_CONNECTED            0x4001  /**< Network Connected Successfully */
#define MSG_NET_DISCONNECTED         0x4002  /**< Network Disconnected */
#define MSG_NET_GOT_IP               0x4003  /**< IP Address Obtained */
#define MSG_NET_WIFI_CONFIG_SUCCESS  0x4004  /**< WiFi provisioning completed */
#define MSG_NET_WIFI_CONFIG_TIMEOUT  0x4005  /**< WiFi provisioning timeout */
#define MSG_NET_WIFI_CONFIG_ERROR    0x4006  /**< WiFi provisioning failed */

#define MSG_SIM_DETECT_CHANGED      0x4007  /**< SIM card detection status changed */
#define MSG_NET_NTP_SUCCESS         0x4008  /**< NTP sync succeeded */
#define MSG_NET_NTP_FAILURE         0x4009  /**< NTP sync failed */

/**
 * @brief MQTT Message ID Definitions (0x5001-)
 */
#define MSG_MQTT_GROUP         0x8000  /**< Grouped MQTT Message Base */
#define MSG_IS_MQTT_GROUP(msg_id)  ((msg_id & MSG_MQTT_GROUP) == MSG_MQTT_GROUP)
#define MSG_MQTT_CONNECTED      0x8001  /**< MQTT Connected Successfully */
#define MSG_MQTT_DISCONNECTED   0x8002  /**< MQTT Disconnected */
#define MSG_MQTT_DATA_ARRIVED   0x8003  /**< MQTT Message Arrived */
#define MSG_MQTT_PUBLISHED      0x8004  /**< MQTT Message Published Successfully */
#define MSG_MQTT_CONNECT_FAILED   0x8006  /**< MQTT Connection Failed */


#define MSG_HTTP_GROUP         0x10000  /**< Grouped HTTP Message Base */
#define MSG_IS_HTTP_GROUP(msg_id)  ((msg_id & MSG_HTTP_GROUP) == MSG_HTTP_GROUP)
#define MSG_HTTP_DOWNLOAD_START      0x10001  /**< HTTP download start */
#define MSG_HTTP_DOWNLOAD_PROGRESS   0x10002  /**< HTTP download in progress */
#define MSG_HTTP_DOWNLOAD_SUCCESS    0x10003  /**< HTTP download success */
#define MSG_HTTP_DOWNLOAD_FAIL       0x10004  /**< HTTP download fail */

#define MSG_UNZIP_GROUP        0x20000  /**< Grouped unzip message base */
#define MSG_IS_UNZIP_GROUP(msg_id) ((msg_id & MSG_UNZIP_GROUP) == MSG_UNZIP_GROUP)
#define MSG_UNZIP_START        0x20001  /**< Unzip start */
#define MSG_UNZIP_PROGRESS     0x20002  /**< Unzip progress */
#define MSG_UNZIP_SUCCESS      0x20003  /**< Unzip success */
#define MSG_UNZIP_FAIL         0x20004  /**< Unzip failure */

#define MSG_MQTT_ERROR          0x8005  /**< MQTT Error */

/**
 * @brief Key State Enumeration
 */
typedef enum {
    KEY_STATE_UP = 0,           /**< Key Released */
    KEY_STATE_DOWN = 1,         /**< Key Pressed */
    KEY_STATE_LONG_PRESS = 2    /**< Key Long Pressed */
} key_state_e;

/**
 * @brief Charge Status Enumeration
 */
typedef enum {
    CHARGE_STATUS_NONE = 0,     /**< Not Charging */
    CHARGE_STATUS_CHARGING = 1, /**< Charging */
    CHARGE_STATUS_FULL = 2,     /**< Fully Charged */
    CHARGE_STATUS_LOW = 3       /**< Low Battery */
} charge_status_e;

/**
 * @brief Message Processing Callback Function Type
 * @param msg Message pointer
 * @return 0=message handled, -1=use default processing
 */
typedef int (*msg_process_cb_t)(framework_msg_t *msg);

/**
 * @brief Initialize Message Management Module
 * @return 0=success, -1=failure
 */
int framework_msg_init(void);

/**
 * @brief Send Message
 * @param msg Message pointer
 * @return 0=success, -1=failure
 */
int framework_msg_send(framework_msg_t *msg);

/**
 * @brief Register Message Processing Callback Function
 * @param callback Callback function pointer
 * @return 0=success, -1=failure
 */
int framework_msg_register_callback(msg_process_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif /* _FRAMEWORK_MSG_H_ */
