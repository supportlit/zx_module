/**
 * @file framework_mqtt.h
 * @brief Framework MQTT client module header
 * @details Provides MQTT connection, subscription, publishing, and message handling with customer extension support
 */

#ifndef _FRAMEWORK_MQTT_H_
#define _FRAMEWORK_MQTT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief MQTT connection state enumeration
 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,    /**< Disconnected */
    MQTT_STATE_CONNECTING = 1,      /**< Connecting */
    MQTT_STATE_CONNECTED = 2,       /**< Connected */
    MQTT_STATE_RECONNECTING = 3     /**< Reconnecting */
} mqtt_state_e;

/**
 * @brief MQTT event type enumeration
 */
typedef enum {
    MQTT_EVENT_CONNECTED = 0,       /**< Connection established */
    MQTT_EVENT_DISCONNECTED = 1,    /**< Connection closed */
    MQTT_EVENT_PUBLISHED = 2,       /**< Message published successfully */
    MQTT_EVENT_DATA = 3,            /**< Message received */
    MQTT_EVENT_ERROR = 4,           /**< Error occurred */
    MQTT_EVENT_SUBSCRIBED = 5,      /**< Subscription successful */
    MQTT_EVENT_UNSUBSCRIBED = 6     /**< Unsubscription successful */
} mqtt_event_e;

/**
 * @brief MQTT authentication type enumeration
 */
typedef enum {
    MQTT_AUTH_NONE = 0,             /**< No authentication */
    MQTT_AUTH_USERNAME_PASSWORD = 1,/**< Username/password authentication */
    MQTT_AUTH_CLIENT_CERT = 2,      /**< Client certificate authentication (mutual TLS) */
    MQTT_AUTH_CUSTOM = 3            /**< Custom authentication */
} mqtt_auth_type_e;

/**
 * @brief MQTT certificate configuration
 */
typedef struct {
    const char *ca_cert;            /**< CA certificate */
    const char *client_cert;        /**< Client certificate */
    const char *client_key;         /**< Client private key */
    uint32_t ca_cert_len;           /**< CA certificate length */
    uint32_t client_cert_len;       /**< Client certificate length */
    uint32_t client_key_len;        /**< Client private key length */
} mqtt_cert_config_t;

/**
 * @brief MQTT Last Will and Testament (LWT) configuration
 */
typedef struct {
    char *topic;                    /**< LWT topic */
    char *message;                  /**< LWT message payload */
    uint8_t qos;                    /**< LWT QoS level */
    uint8_t retain;                 /**< LWT retain flag */
} mqtt_will_config_t;

/**
 * @brief MQTT configuration parameters
 */
typedef struct {
    /* Broker configuration */
    char *host;                     /**< MQTT broker hostname or IP */
    char *sni_host;                 /**< SNI hostname for TLS (optional) */
    uint16_t port;                  /**< MQTT broker port */
    char ip[64];
    /* Client identification */
    char *client_id;                /**< MQTT client ID */
    
    /* Authentication configuration */
    mqtt_auth_type_e auth_type;     /**< Authentication type */
    char *username;                 /**< Username (optional) */
    char *password;                 /**< Password (optional) */
    mqtt_cert_config_t *cert;       /**< Certificate config (optional) */
    
    /* Connection parameters */
    uint16_t keepalive_sec;         /**< Keep-alive interval in seconds */
    uint8_t enable_ssl;             /**< Enable SSL/TLS */
    uint8_t clean_session;          /**< Clean session flag (0=persistent, 1=clean) */
    uint8_t mqtt_version;           /**< MQTT version (3=3.1, 4=3.1.1) */
    
    /* Last Will and Testament */
    mqtt_will_config_t *will;       /**< LWT config (optional) */
    
    /* Default QoS */
    uint8_t default_qos;            /**< Default QoS level for operations */
    char **alpn_list;               /**< ALPN protocols (optional) */
} mqtt_config_t;

/**
 * @brief MQTT message data structure (framework internal use)
 */
typedef struct {
    char *topic;                    /**< Topic string */
    char *payload;                  /**< Message payload */
    uint32_t payload_len;           /**< Payload length in bytes */
    uint8_t qos;                    /**< QoS level (0, 1, or 2) */
    uint8_t retain;                 /**< Retain flag */
} framework_mqtt_message_t;

/**
 * @brief MQTT event callback function type
 * @param event Event type
 * @param data Event-specific data pointer
 */
typedef void (*mqtt_event_callback_t)(mqtt_event_e event, void *data);

/**
 * @brief MQTT message handler callback function type
 * @param topic Topic on which message was received
 * @param payload Message payload buffer
 * @param payload_len Payload length in bytes
 * @return 0=message handled by customer, framework won't process; -1=not handled, framework continues processing
 */
typedef int (*mqtt_message_callback_t)(const char *topic, const char *payload, uint32_t payload_len);

/**
 * @brief MQTT custom configuration callback function type
 * @param config MQTT configuration pointer (customer can modify)
 * @return 0=use modified config; -1=use original config
 */
typedef int (*mqtt_config_callback_t)(mqtt_config_t *config);

/**
 * @brief MQTT reconnect handler callback function type
 */
typedef void (*mqtt_reconnect_handler_t)(void* reconnect_date);

/**
 * @brief Initialize MQTT client
 * @param config MQTT configuration parameters
 * @return 0=success, -1=failure
 */
int framework_mqtt_init(mqtt_config_t *config);

/**
 * @brief Deinitialize MQTT client
 * @return 0=success, -1=failure
 */
int framework_mqtt_deinit(void);

/**
 * @brief Start MQTT connection
 * @return 0=success, -1=failure
 */
int framework_mqtt_start(void);

/**
 * @brief Stop MQTT connection
 * @return 0=success, -1=failure
 */
int framework_mqtt_stop(void);

/**
 * @brief Subscribe to MQTT topic
 * @param topic Topic filter string
 * @param qos QoS level (0/1/2)
 * @return 0=success, -1=failure
 */
int framework_mqtt_subscribe(const char *topic, uint8_t qos);

/**
 * @brief Unsubscribe from MQTT topic
 * @param topic Topic filter string
 * @return 0=success, -1=failure
 */
int framework_mqtt_unsubscribe(const char *topic);

/**
 * @brief Publish MQTT message
 * @param topic Topic string
 * @param payload Message payload buffer
 * @param payload_len Payload length in bytes
 * @param qos QoS level (0/1/2)
 * @param retain Retain flag (1=retain, 0=no retain)
 * @return 0=success, -1=failure
 */
int framework_mqtt_publish(const char *topic, const char *payload, uint32_t payload_len, uint8_t qos, uint8_t retain);

/**
 * @brief Register event callback (customer layer has priority)
 * @param callback Callback function pointer
 * @return 0=success, -1=failure
 */
int framework_mqtt_register_event_callback(mqtt_event_callback_t callback);

/**
 * @brief Register message handler callback (customer layer has priority)
 * @param callback Callback function pointer
 * @return 0=success, -1=failure
 * @note If customer callback returns 0, framework won't process the message
 */
int framework_mqtt_register_message_callback(mqtt_message_callback_t callback);

/**
 * @brief Register config callback (allows customer customization)
 * @param callback Callback function pointer
 * @return 0=success, -1=failure
 */
int framework_mqtt_register_config_callback(mqtt_config_callback_t callback);

/**
 * @brief Get MQTT connection state
 * @return Current MQTT connection state
 */
mqtt_state_e framework_mqtt_get_state(void);

/**
 * @brief Check if MQTT is connected
 * @return 1=connected, 0=not connected
 */
uint8_t framework_mqtt_is_connected(void);

/**
 * @brief Get reconnection attempt count
 * @return Number of reconnection attempts
 */
uint32_t framework_mqtt_get_reconnect_count(void);

/**
 * @brief Reset reconnection counter
 */
void framework_mqtt_reset_reconnect_count(void);

/**
 * @brief Register reconnect callback
 * @param callback Callback function pointer
 * @return 0=success, -1=failure
 */
int framework_mqtt_register_reconnect_callback(mqtt_reconnect_handler_t callback, void *data);

#ifdef __cplusplus
}
#endif

#endif /* _FRAMEWORK_MQTT_H_ */
