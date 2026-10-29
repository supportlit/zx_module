#include "customer_mqtt_handler.h"
#include "cJSON.h"
#include "customer_config.h"
#include "customer_payment_handler.h"
#include "customer_status_report.h"
#include "framework_audio.h"
#include "framework_device_config.h"
#include "framework_http.h"
#include "framework_log.h"
#include "framework_mqtt.h"
#include "framework_msg.h"
#include "framework_unzip.h"
#include "fs_api.h"
#include "mbedtls/md5.h"
#include "osi_api.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "TEMPLATE_MQTT"

#define TEMPLATE_SOUND_ZIP_PATH "D:/template_sound_update.zip"
#define TEMPLATE_SOUND_UNZIP_DEST "D:/"
#define TEMPLATE_SOUND_MD5_MAX 64

#define TEMPLATE_CERTS_DIR_PATH "D:/certs/"
#define TEMPLATE_CA_CERT_PATH TEMPLATE_CERTS_DIR_PATH "AmazonRootCA1.pem"
#define TEMPLATE_CLIENT_CERT_PATH TEMPLATE_CERTS_DIR_PATH "device_cert.pem"
#define TEMPLATE_CLIENT_KEY_PATH TEMPLATE_CERTS_DIR_PATH "device_key.pem"

// #define TEMPLATE_CA_CERT_PATH        TEMPLATE_CERTS_DIR_PATH
// "AmazonRootCA1.pem" #define TEMPLATE_CLIENT_CERT_PATH TEMPLATE_CERTS_DIR_PATH
// "cis-soundbox-certificate.pem.crt" #define TEMPLATE_CLIENT_KEY_PATH
// TEMPLATE_CERTS_DIR_PATH "cis-soundbox-private.pem.key"

// #define MQTT_BROKER_HOST        "iot-dev.template.com"
#define MQTT_BROKER_HOST "a1ha6sbciuji3v-ats.iot.ap-southeast-2.amazonaws.com"
#define MQTT_BROKER_PORT 8883
#define MQTT_USE_TLS 1
#define MQTT_KEEPALIVE_SEC 60

#define TEMPLATE_CA_CERT_MAX_LEN 4096
#define TEMPLATE_CLIENT_CERT_MAX_LEN 4096
#define TEMPLATE_CLIENT_KEY_MAX_LEN 4096

typedef enum {
  TEMPLATE_SOUND_STATE_IDLE = 0,
  TEMPLATE_SOUND_STATE_DOWNLOADING,
  TEMPLATE_SOUND_STATE_UNZIPPING
} template_sound_state_t;

typedef struct {
  template_sound_state_t state;
  char checksum[TEMPLATE_SOUND_MD5_MAX];
} template_sound_update_ctx_t;

static template_sound_update_ctx_t g_sound_update_ctx = {0};

static void template_sound_reset_ctx(void);
static void template_sound_cleanup_files(void);
static void template_sound_update_fail(const char *reason);
static void template_sound_update_success(void);
static int template_calc_file_md5(const char *path, char *md5_out33);
static int template_compare_checksum(const char *expected, const char *actual);
static int template_start_sound_update(const char *url, const char *checksum);
static void template_handle_http_download_msg(framework_msg_t *msg);
static void template_handle_unzip_msg(framework_msg_t *msg);

static int template_load_certificates_from_files(void);
static int template_load_file_into_buffer(const char *path, char *buffer,
                                          uint32_t max_len, uint32_t *out_len);

static char g_template_ca_cert_buf[TEMPLATE_CA_CERT_MAX_LEN] = {0};
static char g_template_client_cert_buf[TEMPLATE_CLIENT_CERT_MAX_LEN] = {0};
static char g_template_client_key_buf[TEMPLATE_CLIENT_KEY_MAX_LEN] = {0};
static uint32_t g_template_ca_cert_len = 0;
static uint32_t g_template_client_cert_len = 0;
static uint32_t g_template_client_key_len = 0;
static int g_template_cert_loaded = 0;
static mqtt_cert_config_t s_template_cert = {0};
static int g_mqtt_initialized = 0;
static int reconnect_count_idx = 0;

// Topics to subscribe after connection
static const char *g_template_topics[] = {
#if defined(CUSTOMER_ENABLE_LCD) && (CUSTOMER_ENABLE_LCD == 1)
    // "device/LIT_PBL_DISP_006/pebble_screen", 
#else
    //"device/LIT_PBL_001/pebble",
    "device/LIT_PKT_005/pocket",
#endif
    "BP1/%s/payment",
    "BP1/%s/sound-update",
    "BP1/%s/ota-update",
    "BP1/%s/restart",
    "BP1/%s/update-lang",
    "BP1/%s/activation"};
static char sn[32] = {0};
static char sub_topic[8][128] = {0};

static void template_sound_reset_ctx(void) {
  g_sound_update_ctx.state = TEMPLATE_SOUND_STATE_IDLE;
  g_sound_update_ctx.checksum[0] = '\0';
}

static void template_sound_cleanup_files(void)

{
  fs_remove(TEMPLATE_SOUND_ZIP_PATH);
  char info_path[96];
  snprintf(info_path, sizeof(info_path), "%s.info", TEMPLATE_SOUND_ZIP_PATH);
  fs_remove(info_path);
}

static int template_calc_file_md5(const char *path, char *md5_out33) {
  if (!path || !md5_out33) {
    return -1;
  }

  int fd = fs_open(path, "rb");
  if (fd < 0) {
    return -2;
  }

  mbedtls_md5_context ctx;
  mbedtls_md5_init(&ctx);
  mbedtls_md5_starts_ret(&ctx);

  uint8_t buf[4096];
  int read_len = 0;
  while ((read_len = fs_read(buf, 1, sizeof(buf), fd)) > 0) {
    mbedtls_md5_update_ret(&ctx, buf, read_len);
  }
  fs_close(fd);

  uint8_t md5_raw[16];
  mbedtls_md5_finish_ret(&ctx, md5_raw);
  mbedtls_md5_free(&ctx);

  for (int i = 0; i < 16; ++i) {
    sprintf(md5_out33 + (i * 2), "%02x", md5_raw[i]);
  }
  md5_out33[32] = '\0';
  return 0;
}

static int template_compare_checksum(const char *expected, const char *actual) {
  if (!expected || expected[0] == '\0') {
    return 0;
  }
  if (!actual || actual[0] == '\0') {
    return -1;
  }

  size_t expected_len = strlen(expected);
  size_t actual_len = strlen(actual);
  if (expected_len != actual_len) {
    return -2;
  }

  for (size_t i = 0; i < expected_len; ++i) {
    if (tolower((unsigned char)expected[i]) !=
        tolower((unsigned char)actual[i])) {
      return -3;
    }
  }
  return 0;
}

static void template_sound_update_fail(const char *reason) {
  OSI_LOGE(TAG, "Sound update failed: %s", reason ? reason : "unknown");
  template_sound_cleanup_files();
  template_sound_reset_ctx();
}

static void template_sound_update_success(void) {
  OSI_LOGI(TAG, "Sound update completed successfully");
  template_sound_cleanup_files();
  template_sound_reset_ctx();
}

static int template_start_sound_update(const char *url, const char *checksum) {
  if (!url || url[0] == '\0') {
    OSI_LOGE(TAG, "[SOUND-UPDATE] url missing");
    return -1;
  }

  if (g_sound_update_ctx.state != TEMPLATE_SOUND_STATE_IDLE) {
    OSI_LOGW(TAG, "[SOUND-UPDATE] request ignored, update already running");
    return -2;
  }

  template_sound_cleanup_files();
  template_sound_reset_ctx();
  if (checksum && checksum[0] != '\0') {
    strncpy(g_sound_update_ctx.checksum, checksum,
            sizeof(g_sound_update_ctx.checksum) - 1);
  }
  g_sound_update_ctx.state = TEMPLATE_SOUND_STATE_DOWNLOADING;

  int ret = framework_http_download_file(url, TEMPLATE_SOUND_ZIP_PATH,
                                         HTTP_DOWNLOAD_TYPE_RES_OTA);
  if (ret != 0) {
    OSI_LOGE(TAG, "[SOUND-UPDATE] failed to queue download (%d)", ret);
    template_sound_reset_ctx();
    return ret;
  }

  OSI_LOGI(TAG, "[SOUND-UPDATE] download started url=%s", url);
  return 0;
}

static void template_handle_http_download_msg(framework_msg_t *msg) {
  if (!msg) {
    return;
  }

  http_download_type_t type = (http_download_type_t)msg->param1;
  if (type != HTTP_DOWNLOAD_TYPE_RES_OTA) {
    // FW FOTA already process in message handler
    return;
  }

  switch (msg->id) {
  case MSG_HTTP_DOWNLOAD_START:
    OSI_LOGI(TAG, "[SOUND-UPDATE] download start %d%%", msg->param2);
    break;
  case MSG_HTTP_DOWNLOAD_PROGRESS:
    OSI_LOGI(TAG, "[SOUND-UPDATE] download progress %d%%", msg->param2);
    break;
  case MSG_HTTP_DOWNLOAD_SUCCESS: {
    if (g_sound_update_ctx.state != TEMPLATE_SOUND_STATE_DOWNLOADING) {
      break;
    }
    const char *zip_path = (const char *)(uintptr_t)msg->param3;
    if (!zip_path || zip_path[0] == '\0') {
      zip_path = TEMPLATE_SOUND_ZIP_PATH;
    }

    if (g_sound_update_ctx.checksum[0] != '\0') {
      char md5_actual[33] = {0};
      if (template_calc_file_md5(zip_path, md5_actual) != 0 ||
          template_compare_checksum(g_sound_update_ctx.checksum, md5_actual) !=
              0) {
        template_sound_update_fail("checksum mismatch");
        return;
      }
      OSI_LOGI(TAG, "[SOUND-UPDATE] checksum verified (%s)", md5_actual);
    }

    int unzip_ret = framework_unzip_start(zip_path, TEMPLATE_SOUND_UNZIP_DEST,
                                          FRAMEWORK_UNZIP_TYPE_RESOURCES);
    if (unzip_ret != 0) {
      OSI_LOGE(TAG, "[SOUND-UPDATE] failed to start unzip (%d)", unzip_ret);
      template_sound_update_fail("unzip queue failed");
      return;
    }
    g_sound_update_ctx.state = TEMPLATE_SOUND_STATE_UNZIPPING;
    break;
  }
  case MSG_HTTP_DOWNLOAD_FAIL:
    template_sound_update_fail("download failed");
    break;
  default:
    break;
  }
}

static void template_handle_unzip_msg(framework_msg_t *msg) {
  if (!msg) {
    return;
  }

  framework_unzip_type_t type = (framework_unzip_type_t)msg->param1;
  if (type != FRAMEWORK_UNZIP_TYPE_RESOURCES) {
    return;
  }

  switch (msg->id) {
  case MSG_UNZIP_START:
    OSI_LOGI(TAG, "[SOUND-UPDATE] unzip started");
    break;
  case MSG_UNZIP_PROGRESS:
    OSI_LOGI(TAG, "[SOUND-UPDATE] unzip progress %d%%", msg->param2);
    break;
  case MSG_UNZIP_SUCCESS:
    if (g_sound_update_ctx.state == TEMPLATE_SOUND_STATE_UNZIPPING) {
      template_sound_update_success();
    }
    break;
  case MSG_UNZIP_FAIL:
    template_sound_update_fail("unzip failed");
    break;
  default:
    break;
  }
}

static int template_load_file_into_buffer(const char *path, char *buffer,
                                          uint32_t max_len, uint32_t *out_len) {
  if (!path || !buffer || max_len < 2) {
    return -1;
  }

  int fd = fs_open(path, "rb");
  if (fd < 0) {
    OSI_LOGE(TAG, "[CERT] open failed: %s", path);
    return -2;
  }

  uint32_t total = 0;
  while (total < (max_len - 1)) {
    int read_len = fs_read(buffer + total, 1, (int)(max_len - 1 - total), fd);
    if (read_len <= 0) {
      break;
    }
    total += (uint32_t)read_len;
  }
  fs_close(fd);

  if (total == 0) {
    OSI_LOGE(TAG, "[CERT] file empty: %s", path);
    return -3;
  }

  if (total >= (max_len - 1)) {
    OSI_LOGE(TAG, "[CERT] file too large: %s (max %lu)", path,
             (unsigned long)(max_len - 1));
    return -4;
  }

  buffer[total] = '\0';
  if (out_len) {
    *out_len = total;
  }
  return 0;
}

static int template_load_certificates_from_files(void) {
  if (g_template_cert_loaded) {
    return 0;
  }

  if (template_load_file_into_buffer(
          TEMPLATE_CA_CERT_PATH, g_template_ca_cert_buf,
          sizeof(g_template_ca_cert_buf), &g_template_ca_cert_len) != 0) {
    return -1;
  }

  if (template_load_file_into_buffer(TEMPLATE_CLIENT_CERT_PATH,
                                     g_template_client_cert_buf,
                                     sizeof(g_template_client_cert_buf),
                                     &g_template_client_cert_len) != 0) {
    return -2;
  }

  if (template_load_file_into_buffer(
          TEMPLATE_CLIENT_KEY_PATH, g_template_client_key_buf,
          sizeof(g_template_client_key_buf), &g_template_client_key_len) != 0) {
    return -3;
  }

  s_template_cert.ca_cert = g_template_ca_cert_buf;
  s_template_cert.client_cert = g_template_client_cert_buf;
  s_template_cert.client_key = g_template_client_key_buf;
  s_template_cert.ca_cert_len = g_template_ca_cert_len;
  s_template_cert.client_cert_len = g_template_client_cert_len;
  s_template_cert.client_key_len = g_template_client_key_len;

  g_template_cert_loaded = 1;
  OSI_LOGI(TAG, "[CERT] Certificates loaded from filesystem");
  return 0;
}

static void template_handle_server_message(const char *topic,
                                           const char *payload,
                                           uint32_t payload_len) {
  char buf[512];
  if (payload_len >= sizeof(buf))
    payload_len = sizeof(buf) - 1;
  memcpy(buf, payload, payload_len);
  buf[payload_len] = 0;

  cJSON *root = cJSON_Parse(buf);
  if (!root) {
    OSI_LOGI(TAG, "[JSON] Parse error: %s", buf);
    return;
  }

  if (strstr(topic, "/payment") || strstr(topic, "pebble") ||
      strstr(topic, "pebble_screen") || strstr(topic, "pocket") || strstr(topic, "neo")) {
    // {"transaction_id":"...", "amount":..., "status":"SUCCESS"}
    const cJSON *j_id = cJSON_GetObjectItem(root, "transaction_id");
    const cJSON *j_amount = cJSON_GetObjectItem(root, "amount");
    const cJSON *j_status = cJSON_GetObjectItem(root, "status");
    const char *transaction_id =
        j_id && cJSON_IsString(j_id) ? j_id->valuestring : "";
    // add by vipin: parse amount supporting float number, string and integer
    // (e.g. 12.50, "12.50", 12)
    double amount_val = 0.0;
    if (j_amount) {
      if (cJSON_IsNumber(j_amount)) {
        amount_val = j_amount->valuedouble;
      } else if (cJSON_IsString(j_amount) && j_amount->valuestring) {
        amount_val = atof(j_amount->valuestring);
      }
    }
    uint64_t rupees = (uint64_t)amount_val;
    uint32_t paise = (uint32_t)((amount_val - (double)rupees) * 100.0 + 0.5);
    if (paise >= 100) {
      rupees += 1;
      paise = 0;
    }

    const char *status =
        j_status && cJSON_IsString(j_status) ? j_status->valuestring : "";
    OSI_LOGI(TAG,
             "[PAYMENT] id=%s, amount=%.2f (rupees=%llu, paise=%lu), status=%s",
             transaction_id, amount_val, (unsigned long long)rupees,
             (unsigned long)paise, status); // add by vipin
    payment_broadcast_params_t params = {0};
    params.amount = rupees; // add by vipin: legacy integer field compatibility
    params.rupees = rupees; // add by vipin: integer rupee portion
    params.paise = paise;   // add by vipin: integer paise portion (0-99)
    params.total_amount = amount_val; // add by vipin: full amount float/double
    int ret = customer_payment_broadcast(&params);
    payment_callback_t callback = {0};
    strncpy(callback.serial_number, sn, sizeof(callback.serial_number) - 1);
    strncpy(callback.status, ret == 0 ? "SUCCESS" : "FAILURE",
            sizeof(callback.status) - 1);
    strncpy(callback.transaction_id, transaction_id,
            sizeof(callback.transaction_id) - 1);
    customer_status_report_payment(&callback);
  } else if (strstr(topic, "/sound-update")) {
    const cJSON *j_url = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *j_md5 = cJSON_GetObjectItemCaseSensitive(root, "MD5");
    const char *url =
        (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : NULL;
    const char *md5 =
        (j_md5 && cJSON_IsString(j_md5)) ? j_md5->valuestring : NULL;
    OSI_LOGI(TAG, "[SOUND-UPDATE] url=%s md5=%s", url ? url : "(null)",
             md5 ? md5 : "(none)");
    if (template_start_sound_update(url, md5) != 0) {
      OSI_LOGE(TAG, "[SOUND-UPDATE] request rejected");
    }
  } else if (strstr(topic, "/ota-update")) {
    // {"url":"..."}
    const cJSON *j_url = cJSON_GetObjectItem(root, "url");
    const cJSON *j_type = cJSON_GetObjectItem(root, "type");
    const char *url = j_url && cJSON_IsString(j_url) ? j_url->valuestring : "";
    OSI_LOGI(TAG, "[OTA-UPDATE]type:%s url=%s",
             j_type ? j_type->valuestring : "(none)", url);
    if (j_type && cJSON_IsString(j_type)) {
      const char *type = j_type->valuestring;
      OSI_LOGI(TAG, "[OTA-UPDATE] url=%s type=%s", url, type);
      if (strcmp(type, "wifi-firmware") == 0) {
        framework_http_download_file(url, "D:/wifi_firmware.bin",
                                     HTTP_DOWNLOAD_TYPE_WIFI_FW_OTA);
        audio_resource_play_combo_async_mid_priority("", "upgrade-ing.mp3",
                                                     NULL, 1);
        cJSON_Delete(root);
        return;
      }
    }
    framework_http_download_file(url, "D:/ota_update.bin",
                                 HTTP_DOWNLOAD_TYPE_FW_OTA);
    audio_resource_play_combo_async_mid_priority("", "upgrade-ing.mp3", NULL,
                                                 1);
  } else if (strstr(topic, "/restart")) {
    // {"restart":"true"}
    const cJSON *j_restart = cJSON_GetObjectItem(root, "restart");
    const char *restart =
        j_restart && cJSON_IsString(j_restart) ? j_restart->valuestring : "";
    OSI_LOGI(TAG, "[RESTART] restart=%s", restart);
    if (strcmp(restart, "true") == 0) {
      osiSysReset();
    }
  } else if (strstr(topic, "/update-lang")) {
    const cJSON *j_lang = cJSON_GetObjectItem(root, "lang_support");
    const char *lang =
        j_lang && cJSON_IsString(j_lang) ? j_lang->valuestring : "";
    OSI_LOGI(TAG, "[LANG-UPDATE] lang_support=%s", lang);
    char prefix[32] = {0};
    sprintf(prefix, "D:/%s/", lang);
    audio_resource_set_language_prefix(prefix);
  } else if (strstr(topic, "/activation")) {
    // {"status":"SUCCESS"}
    const cJSON *j_status = cJSON_GetObjectItem(root, "status");
    const char *status =
        j_status && cJSON_IsString(j_status) ? j_status->valuestring : "";
    OSI_LOGI(TAG, "[ACTIVATION] status=%s", status);
    audio_resource_play_combo_async_mid_priority(
        PAYMENT_AUDIO_FOLDER, "activation-device.mp3", NULL, 1);
  } else {
    OSI_LOGI(TAG, "Received message on unknown topic: %s", topic);
  }
  cJSON_Delete(root);
}

// MQTT message callback
static int template_mqtt_message_handler(const char *topic, const char *payload,
                                         uint32_t payload_len) {
  OSI_LOGI(TAG, "MQTT message: topic=%s, payload=%.*s", topic, payload_len,
           payload);
  template_handle_server_message(topic, payload, payload_len);
  return 0; // Mark as handled
}

// MQTT event callback
static void template_mqtt_event_callback(mqtt_event_e event, void *data) {
  if (event == MQTT_EVENT_CONNECTED) {
    OSI_LOGI(TAG, "MQTT connected, subscribing to topics [%d]...",
             sizeof(g_template_topics) / sizeof(g_template_topics[0]));
    for (int i = 0;
         i < sizeof(g_template_topics) / sizeof(g_template_topics[0]); ++i) {
      sprintf(sub_topic[i], g_template_topics[i], sn);
      int ret = framework_mqtt_subscribe(sub_topic[i], 1);
      OSI_LOGI(TAG, "Subscribe %s: %d", sub_topic[i], ret);
    }

    // Start status reporting after MQTT connection is established
    OSI_LOGI(TAG, "Starting status report (interval: 30 sec)");
    customer_status_report_start(30);
  } else if (event == MQTT_EVENT_DISCONNECTED) {
    OSI_LOGW(TAG, "MQTT disconnected, stopping status report");
    // customer_status_report_stop();
  }
}

static int customer_mqtt_message_handler(const char *topic, const char *payload,
                                         uint32_t payload_len) {
  if (!payload || payload_len == 0)
    return -1;
  OSI_LOGI(TAG, "Received command: %.*s", (int)payload_len, payload);
  // TODO: 解析 payload 并调用 customer_payment_broadcast
  return 0;
}

static void template_mqtt_reconnect_handler(uint32_t reconnect_date) {
  static int reconnect_count_list[10] = {5,   10,  30,  60,   120,
                                         240, 360, 720, 1440, 2880};
  OSI_LOGI(TAG,
           "MQTT reconnect handler called, reconnect_date=%d is_connected=%d",
           reconnect_date, framework_network_is_connected());
  while (!framework_network_is_connected()) {
    osiThreadSleep(200);
  }
}

static int customer_mqtt_config_handler(mqtt_config_t *config) {
  if (!config) {
    return -1;
  }

  if (template_load_certificates_from_files() != 0) {
    OSI_LOGE(TAG, "[CERT] load failed");
    return -1;
  }
  // 2	2025-12-03 09:48:21.057000	116.116.116.116	10.11.142.235	DNS
  // 237	Standard query response 0x0000 A iot-dev.template.com CNAME
  // a3uosz02c54meq-ats.iot.ap-southeast-1.amazonaws.com A 13.228.147.92
  // A 3.0.121.83[Packet size limited during capture]
  config->host = strdup(MQTT_BROKER_HOST);
  config->port = MQTT_BROKER_PORT;
  config->enable_ssl = MQTT_USE_TLS;
  config->username = NULL; // AWS IoT requires username to be NULL
  config->password = NULL; // AWS IoT requires password to be NULL
  config->client_id = sn;
  config->mqtt_version = 4; // MQTT 3.1.1
  config->keepalive_sec = 120;
  config->clean_session = 1;
  config->default_qos = 1;
  config->auth_type = MQTT_AUTH_CLIENT_CERT;
  config->cert = &s_template_cert;
  config->sni_host = strdup(MQTT_BROKER_HOST);
  return 0;
}

int customer_mqtt_init(void) {
  if (g_mqtt_initialized) {
    // OSI_LOGI(TAG, "MQTT already initialized! state: %d",
    // framework_mqtt_get_state()); if(framework_mqtt_get_state() ==
    // MQTT_STATE_DISCONNECTED) {
    //     if (framework_mqtt_start() != 0) {
    //         OSI_LOGI(TAG, "Failed to start MQTT connection");
    //         return -1;
    //     }
    //     return 0;
    // }
    return 0;
  }

  // Initialize status report module
  if (customer_status_report_init() != 0) {
    OSI_LOGE(TAG, "Failed to initialize status report module");
    return -1;
  }

  framework_device_config_get_sn(sn, sizeof(sn));
  framework_mqtt_register_config_callback(customer_mqtt_config_handler);
  framework_mqtt_register_message_callback(template_mqtt_message_handler);
  framework_mqtt_register_event_callback(template_mqtt_event_callback);
  framework_mqtt_register_reconnect_callback(template_mqtt_reconnect_handler,
                                             NULL);
  mqtt_config_t config = {0};
  config.keepalive_sec = 120;
  config.clean_session = 1;
  reconnect_count_idx = 0;

  if (framework_mqtt_init(&config) != 0) {
    OSI_LOGI(TAG, "Failed to initialize framework MQTT");
    return -1;
  }
  if (framework_mqtt_start() != 0) {
    OSI_LOGI(TAG, "Failed to start MQTT connection");
    return -1;
  }
  OSI_LOGI(TAG, "template MQTT handler initialized successfully");
  g_mqtt_initialized = 1;
  return 0;
}

int customer_mqtt_network_ready(void) {
  OSI_LOGI(TAG, "Network ready notification received");
  return 0;
}

void template_mqtt_handle_http_msg(framework_msg_t *msg) {
  template_handle_http_download_msg(msg);
}

void template_mqtt_handle_unzip_msg(framework_msg_t *msg) {
  template_handle_unzip_msg(msg);
}
