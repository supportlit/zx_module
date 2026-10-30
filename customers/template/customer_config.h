/**
 * @file customer_config.h
 * @brief template Customer Configuration
 *
 * 用于覆盖 framework_config.h 的默认配置。
 */

#ifndef __CUSTOMER_CONFIG_H__
#define __CUSTOMER_CONFIG_H__

#define CUSTOMER_NAME "template"

// LCD 功能开关 (1: Enable, 0: Disable)
#define CUSTOMER_ENABLE_LCD 0

#define LED_GREEN_PIN 85
#define LED_GREEN_PINMUX GPIO_VCXO_REQ
#define LED_GREEN_FUN MFP_AF1

#define LED_RED_PIN 2
#define LED_RED_PINMUX GPIO_02
#define LED_RED_FUN MFP_AF0

#define KEY_UP_PIN 33
#define KEY_DOWN_PIN 34

#define KEY_DOWN_PINMUX (GPIO_34)
#define KEY_UP_PINMUX (GPIO_33)

#define KEY_DOWN_FUN (MFP_AF0)
#define KEY_UP_FUN (MFP_AF0)

// MQTT 服务器配置（根据 info.txt 或实际需求调整

// Onboarding API 配置
#define ONBOARD_API_HOST "template-api.example.com"
#define ONBOARD_API_URL "/api/v1/devicepool/onboard"
#define ONBOARD_API_PORT 443

// 音频资源路径（仅保留 FEMALE/MALE，默认 FEMALE）
#define AUDIO_LANG_FEMALE_PATH "D:/FEMALE/"
#define AUDIO_LANG_MALE_PATH "D:/MALE/"
#define DEFAULT_LANGUAGE 0

// 电池参数
#define LOW_POWER_THRESHOLD_MV 3300
#define CRITICAL_LOW_VOLT_MV 3100
#define BAT_VOLTAGE_UPDATE_SEC 30

// 音量参数
#define VOL_MIN 1
#define VOL_MAX 8
#define VOL_STEP 1
#define DEFAULT_VOL 1

// 心跳包配置
#define HEARTBEAT_INTERVAL_SEC 60

// ============================================================================
// TMS HTTPS Heartbeat Configuration
// ============================================================================
#define TMS_HEARTBEAT_ENABLE          1
#define TMS_HEARTBEAT_INTERVAL_SEC    60  /* Heartbeat interval in seconds (e.g. 30, 60, 120, 300) */
#define TMS_HEARTBEAT_URL             "https://103.157.22.211:8075/sxtms/online/tms/saveHB"
#define TMS_HEARTBEAT_AUTH_TOKEN      "f1c3a7e8b9d5e2a0dh7d8f9dh97d"
#define TMS_HEARTBEAT_MID             "000001"
#define TMS_HEARTBEAT_TID             "11110003"
#define TMS_HEARTBEAT_MODEL           "LIT001"
#define TMS_HEARTBEAT_DID             "00020024422317237344"
#define TMS_HEARTBEAT_CPU             "B26747802400E59FFE0B2AAF7B000000"
#define TMS_HEARTBEAT_ACT_STATUS      "REGISTERED"
#define TMS_HEARTBEAT_CONFIG_VER      "1.1"
#define TMS_HEARTBEAT_ANDROID_VER     "10"
#define TMS_HEARTBEAT_APK_VER         "1.0.1"
#define TMS_HEARTBEAT_APP_VER         "1.0.1"
#define TMS_HEARTBEAT_FIRMWARE_VER    "1.0.1"
#define TMS_HEARTBEAT_TSTATUS         "Active"

// 设备信息存储路径
#define DEVICE_INFO_PATH "D:/template_device.cfg"
#define DEVICE_SN_PATH "D:/template_sn.cfg"
#define MQTT_AUTH_PATH "D:/template_mqtt.cfg"

#define CONFIG_MOD_WIFI 1
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_DEBUG 1
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET 1
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_SSID "NPWIFITEST2"
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_PASSWORD "np72206220"
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_IP "192.168.1.1"
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_DHCP_IP_MIN            \
  "192.168.1.2"
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_DHCP_IP_MAX            \
  "192.168.1.254"
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_ESP8266 1
#define CONFIG_LOSCFG_COMPONENTS_NET_AT_ESP8266_HZR_SPK 1
#define LOSCFG_COMPONENTS_NET_AT_ESP8266 1

#endif /* __CUSTOMER_CONFIG_H__ */
