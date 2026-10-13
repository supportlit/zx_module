/**
 * @file framework_network.h
 * @brief Framework layer network management interface
 */

#ifndef FRAMEWORK_NETWORK_H
#define FRAMEWORK_NETWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network state enumeration
 */
typedef enum {
    NETWORK_STATE_DISCONNECTED = 0,    /**< Disconnected */
    NETWORK_STATE_CONNECTING,          /**< Connecting */
    NETWORK_STATE_CONNECTED,           /**< Connected (registered to network) */
    NETWORK_STATE_PDP_ACTIVATED,       /**< PDP context activated */
    NETWORK_STATE_ERROR                /**< Error state */
} network_state_e;

/**
 * @brief Preferred bearers supported by the framework
 */
typedef enum {
    FRAMEWORK_NETWORK_BEARER_LTE = 0,  /**< Default LTE/4G modem */
    FRAMEWORK_NETWORK_BEARER_WIFI      /**< WiFi station */
} framework_network_bearer_e;

/**
 * @brief Network event enumeration
 * @note Events are now sent via framework message system (MSG_NET_CONNECTED, MSG_NET_DISCONNECTED)
 *       instead of callbacks. This enum is kept for reference.
 */
typedef enum {
    NETWORK_EVENT_DISCONNECTED = 0,    /**< Network disconnected */
    NETWORK_EVENT_CONNECTED,           /**< Network connection established */
    NETWORK_EVENT_PDP_ACTIVATED,       /**< PDP context activated successfully */
    NETWORK_EVENT_ERROR                /**< Network error occurred */
} network_event_e;

/**
 * @brief WiFi provisioning mode
 */
typedef enum {
    FRAMEWORK_WIFI_CONFIG_MODE_NONE = 0,   /**< Provisioning idle */
    FRAMEWORK_WIFI_CONFIG_MODE_WEB_AP,     /**< Local AP/Web portal */
    FRAMEWORK_WIFI_CONFIG_MODE_SMARTCONFIG /**< SmartConfig/Airkiss */
} framework_wifi_config_mode_e;

/**
 * @brief SoftAP language prompt when showing portal pages
 */
typedef enum {
    FRAMEWORK_WIFI_CONFIG_LANG_ENGLISH = 0,
    FRAMEWORK_WIFI_CONFIG_LANG_CHINESE = 1
} framework_wifi_config_language_e;

/**
 * @brief WiFi provisioning start parameters
 */
typedef struct {
    framework_wifi_config_mode_e mode;         /**< Provisioning mode */
    uint32_t timeout_ms;                       /**< Optional timeout, 0 = no timeout */
    const char *softap_ssid;                   /**< Custom SoftAP SSID (WEB mode only) */
    const char *softap_password;               /**< Custom SoftAP password (WEB mode only) */
    int softap_channel;                        /**< SoftAP channel, 0 uses default */
    framework_wifi_config_language_e language; /**< Portal language */
} framework_wifi_config_request_t;

/**
 * @brief Result returned by provisioning poll API
 */
typedef struct {
    framework_wifi_config_mode_e mode; /**< Active mode */
    bool success;                      /**< true if finished with credentials */
    char ssid[33];                     /**< Configured SSID */
    char password[65];                 /**< Configured password */
} framework_wifi_config_result_t;

#define FRAMEWORK_NTP_MAX_SERVERS 3

typedef enum {
    FRAMEWORK_NTP_RESULT_OK = 0,
    FRAMEWORK_NTP_ERR_NOT_CONNECTED = 1,
    FRAMEWORK_NTP_ERR_DNS = 2,
    FRAMEWORK_NTP_ERR_SOCKET = 3,
    FRAMEWORK_NTP_ERR_SEND = 4,
    FRAMEWORK_NTP_ERR_RECV = 5,
    FRAMEWORK_NTP_ERR_BAD_RESPONSE = 6
} framework_ntp_result_e;

typedef struct {
    const char *servers[FRAMEWORK_NTP_MAX_SERVERS];
    size_t server_count;
    uint32_t interval_seconds;
    uint32_t timeout_ms;
} framework_ntp_config_t;

/**
 * @brief Initialize network management module
 * @return 0 on success, -1 on failure
 */
int framework_network_init(void);

/**
 * @brief Start network connection
 * @return 0 on success, -1 on failure
 */
int framework_network_start(void);

/**
 * @brief set network connection
 * @return 0 on success, -1 on failure
 */
int framework_network_start_set(void);
/**
 * @brief Stop network connection
 * @return 0 on success, -1 on failure
 */
int framework_network_stop(void);

/**
 * @brief Get network state
 * @return Current network state
 */
network_state_e framework_network_get_state(void);

/**
 * @brief Check if network is connected
 * @return true if connected, false otherwise
 */
bool framework_network_is_connected(void);

/**
 * @brief Check if PDP context is activated
 * @return true if activated, false otherwise
 */
bool framework_network_is_pdp_activated(void);

/**
 * @brief Get signal strength (CSQ)
 * @return CSQ value (0-31), 99 indicates unknown
 */
int framework_network_get_csq(void);

/**
 * @brief Get IMEI
 * @return Pointer to IMEI string
 */
const char* framework_network_get_imei(void);

/**
 * @brief Get ICCID
 * @return Pointer to ICCID string
 */
const char* framework_network_get_iccid(void);

/**
 * @brief Get current preferred bearer (LTE/WiFi)
 */
framework_network_bearer_e framework_network_get_bearer(void);

/**
 * @brief Switch preferred bearer
 * @return 0 on success, negative value on failure
 */
int framework_network_switch_bearer(framework_network_bearer_e bearer);
int framework_network_switch_bearer_sync(framework_network_bearer_e bearer);
/**
 * @brief Enable WiFi hardware (idempotent)
 */
int framework_network_wifi_open(void);

/**
 * @brief Disable WiFi hardware and disconnect if needed
 */
int framework_network_wifi_close(void);

/**
 * @brief Query WiFi power state
 */
bool framework_network_wifi_is_open(void);

/**
 * @brief Query WiFi link state
 */
bool framework_network_wifi_is_connected(void);

/**
 * @brief Start WiFi provisioning (SoftAP or SmartConfig)
 */
int framework_network_wifi_config_start(const framework_wifi_config_request_t *request);

/**
 * @brief Stop current provisioning flow (if any)
 */
int framework_network_wifi_config_stop(void);

/**
 * @brief Poll provisioning status/result
 * @param result Optional buffer to receive status/output
 * @return 1=success, 0=still running, negative=error/timeout
 */
int framework_network_wifi_config_poll(framework_wifi_config_result_t *result);

/**
 * @brief Set APN for cellular network
 * @param apn APN name (required, max 63 chars)
 * @param username Optional username (can be NULL)
 * @param password Optional password (can be NULL)
 * @return 0 on success, -1 on failure
 */
int framework_network_set_apn(const char *apn, const char *username, const char *password);

/**
 * @brief Get current APN configuration
 * @param apn Buffer to receive APN name (min 64 bytes)
 * @return 0 on success, -1 on failure
 */
int framework_network_get_apn(char *apn, int max_len);

/**
 * @brief set ap web mode ssid
 * @param ssid Buffer to receive ssid (max 32 bytes)
 * @return 0 on success, -1 on failure
 */
int framework_set_apweb_ssid(char *ssid);

int framework_network_ntp_configure(const framework_ntp_config_t *config);
int framework_network_ntp_sync_now(bool sync);

/**
 * @brief set ap web mode ssid
 * @param ssid Buffer to receive ssid (max 32 bytes)
 * @return 0 on success, -1 on failure
 */
int framework_set_apweb_ssid(char *ssid);

const char* framework_network_get_cgpaddr(void);
int framework_network_get_lac(void);
int framework_network_sim_get_status(void);
bool framework_network_wifi_connected(void);
#ifdef __cplusplus
}
#endif

#endif // FRAMEWORK_NETWORK_H
