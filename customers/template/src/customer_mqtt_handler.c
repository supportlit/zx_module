#include "customer_mqtt_handler.h"
#include "soundbox_language.h"
#include "cJSON.h"
#include "customer_config.h"
#include "customer_language_ack.h"
#include "customer_change_language_ack.h"
#include "customer_payment_handler.h"
#include "customer_status_report.h"
#include "customer_app_update.h"
#include "customer_audio_download.h"
#include "framework_audio.h"
#include "framework_device_config.h"
#include "framework_http.h"
#include "framework_log.h"
#include "framework_mqtt.h"
#include "framework_network.h"
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
// Language file paths defined in soundbox_language.h

#define TEMPLATE_CERTS_DIR_PATH "D:/certs/"
#define TEMPLATE_CA_CERT_PATH TEMPLATE_CERTS_DIR_PATH "AmazonRootCA1.pem"
#define TEMPLATE_CLIENT_CERT_PATH TEMPLATE_CERTS_DIR_PATH "device_cert.pem"
#define TEMPLATE_CLIENT_KEY_PATH TEMPLATE_CERTS_DIR_PATH "device_key.pem"

// #define TEMPLATE_CA_CERT_PATH        TEMPLATE_CERTS_DIR_PATH
// "AmazonRootCA1.pem" #define TEMPLATE_CLIENT_CERT_PATH TEMPLATE_CERTS_DIR_PATH
// "cis-soundbox-certificate.pem.crt" #define TEMPLATE_CLIENT_KEY_PATH
// TEMPLATE_CERTS_DIR_PATH "cis-soundbox-private.pem.key"

// #define MQTT_BROKER_HOST        "iot-dev.template.com"
#define MQTT_BROKER_HOST "a3e38vloi5hkal-ats.iot.ap-south-1.amazonaws.com"
#define MQTT_BROKER_PORT 8883
#define MQTT_USE_TLS 0
#define MQTT_KEEPALIVE_SEC 60

#define TEMPLATE_CA_CERT_MAX_LEN 4096
#define TEMPLATE_CLIENT_CERT_MAX_LEN 4096
#define TEMPLATE_CLIENT_KEY_MAX_LEN 4096
mqtt_register_info mqtt_reg = {0};
typedef enum
{
  TEMPLATE_SOUND_STATE_IDLE = 0,
  TEMPLATE_SOUND_STATE_DOWNLOADING,
  TEMPLATE_SOUND_STATE_UNZIPPING
} template_sound_state_t;

typedef struct
{
  template_sound_state_t state;
  char checksum[TEMPLATE_SOUND_MD5_MAX];
  char unzip_dest[64];
  char pending_lang[16];
  bool is_sound_update_topic;
  double request_id;
  char uuid[48];
  char device_id[48];
} template_sound_update_ctx_t;

static template_sound_update_ctx_t g_sound_update_ctx = {0};

static void template_sound_reset_ctx(void);
static void template_sound_cleanup_files(void);
static void template_sound_update_fail(const char *reason);
static void template_sound_update_success(void);
static int template_calc_file_md5(const char *path, char *md5_out33);
static int template_compare_checksum(const char *expected, const char *actual);
int template_start_sound_update(const char *url, const char *checksum, const char *dst, const char *lang,
                                bool is_sound_update_topic,
                                double request_id, const char *uuid, const char *device_id);
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
static char *prv_str_trim(char *str)
{
  if (!str)
    return "";
  while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')
    str++;
  if (*str == 0)
    return str;
  char *end = str + strlen(str) - 1;
  while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
  {
    *end = '\0';
    end--;
  }
  return str;
}

void customer_language_init(void)
{
  soundbox_language_init();
}

const char *customer_get_default_language(void)
{
  return soundbox_language_to_str(soundbox_get_language());
}

void customer_set_default_language(const char *lang)
{
  if (lang && lang[0] != '\0')
  {
    soundbox_language_t l;
    if (soundbox_str_to_language(lang, &l))
    {
      soundbox_set_language(l, true);
    }
    else
    {
      OSI_LOGW(TAG, "[LANG] Unrecognized language code '%s'", lang);
    }
  }
}

// Topics to subscribe after connection
static const char *g_template_topics[] = {
#if defined(CUSTOMER_ENABLE_LCD) && (CUSTOMER_ENABLE_LCD == 1)
    "device/LIT_PBL_DISP_006/pebble_screen",
#else
    //"device/LIT_PBL_001/pebble",
    "device/LIT_PKT_005/pocket",
#endif
    "BP1/%s/sound-update",
    "BP1/%s/ota-update",
    "BP1/%s/restart",
    "BP1/%s/update-lang",
    "BP1/%s/activation",
    "BP1/%s/audio-download",
    "/audio-download"};
static char sn[32] = {0};
static char sub_topic[16][128] = {0};

static void template_sound_reset_ctx(void)
{
  g_sound_update_ctx.state = TEMPLATE_SOUND_STATE_IDLE;
  g_sound_update_ctx.checksum[0] = '\0';
  g_sound_update_ctx.unzip_dest[0] = '\0';
  g_sound_update_ctx.pending_lang[0] = '\0';
  g_sound_update_ctx.is_sound_update_topic = false;
  g_sound_update_ctx.request_id = 0;
  g_sound_update_ctx.uuid[0] = '\0';
  g_sound_update_ctx.device_id[0] = '\0';
}

static void template_sound_cleanup_files(void)

{
  fs_remove(TEMPLATE_SOUND_ZIP_PATH);
  char info_path[96];
  snprintf(info_path, sizeof(info_path), "%s.info", TEMPLATE_SOUND_ZIP_PATH);
  fs_remove(info_path);
}

static int template_calc_file_md5(const char *path, char *md5_out33)
{
  if (!path || !md5_out33)
  {
    return -1;
  }

  int fd = fs_open(path, "rb");
  if (fd < 0)
  {
    return -2;
  }

  mbedtls_md5_context ctx;
  mbedtls_md5_init(&ctx);
  mbedtls_md5_starts_ret(&ctx);

  uint8_t buf[4096];
  int read_len = 0;
  while ((read_len = fs_read(buf, 1, sizeof(buf), fd)) > 0)
  {
    mbedtls_md5_update_ret(&ctx, buf, read_len);
  }
  fs_close(fd);

  uint8_t md5_raw[16];
  mbedtls_md5_finish_ret(&ctx, md5_raw);
  mbedtls_md5_free(&ctx);

  for (int i = 0; i < 16; ++i)
  {
    sprintf(md5_out33 + (i * 2), "%02x", md5_raw[i]);
  }
  md5_out33[32] = '\0';
  return 0;
}

static int template_compare_checksum(const char *expected, const char *actual)
{
  if (!expected || expected[0] == '\0')
  {
    return 0;
  }
  if (!actual || actual[0] == '\0')
  {
    return -1;
  }

  size_t expected_len = strlen(expected);
  size_t actual_len = strlen(actual);
  if (expected_len != actual_len)
  {
    return -2;
  }

  for (size_t i = 0; i < expected_len; ++i)
  {
    if (tolower((unsigned char)expected[i]) !=
        tolower((unsigned char)actual[i]))
    {
      return -3;
    }
  }
  return 0;
}

static void template_sound_update_fail(const char *reason)
{
  OSI_LOGE(TAG, "Sound update failed: %s", reason ? reason : "unknown");

  // Send DownloadLanguageacknowledgement HTTP request for failure ONLY for /sound-update
  if (g_sound_update_ctx.is_sound_update_topic)
  {
    customer_send_language_ack(g_sound_update_ctx.request_id,
                               g_sound_update_ctx.uuid,
                               (g_sound_update_ctx.device_id[0] != '\0') ? g_sound_update_ctx.device_id : sn,
                               "Failed",
                               reason ? reason : "unknown");
  }

  template_sound_cleanup_files();
  template_sound_reset_ctx();
}

static void template_sound_update_success(void)
{
  OSI_LOGI(TAG, "Sound update completed successfully");

  // Send DownloadLanguageacknowledgement HTTP request for success ONLY for /sound-update
  if (g_sound_update_ctx.is_sound_update_topic)
  {
    customer_send_language_ack(g_sound_update_ctx.request_id,
                               g_sound_update_ctx.uuid,
                               (g_sound_update_ctx.device_id[0] != '\0') ? g_sound_update_ctx.device_id : sn,
                               "Success",
                               "");
  }

  // Update default language only after audio is unzipped successfully
  if (g_sound_update_ctx.pending_lang[0] != '\0')
  {
    customer_set_default_language(g_sound_update_ctx.pending_lang);
    OSI_LOGI(TAG, "[SOUND-UPDATE] Unzip successful! Updated language to: %s (folder: %s)",
             customer_get_default_language(), soundbox_get_current_audio_folder());
  }

  if (fs_size("D:/FEMALE/upgrade-success.mp3") > 0 || fs_size("D:/upgrade-success.mp3") > 0)
  {
    audio_resource_play_combo_async_mid_priority("", "upgrade-success.mp3", NULL, 1);
  }
  else
  {
    OSI_LOGI(TAG, "upgrade-success.mp3 not found, skipping success prompt");
  }
  template_sound_cleanup_files();
  template_sound_reset_ctx();
}

int template_start_sound_update(const char *url, const char *checksum, const char *dst, const char *lang,
                                bool is_sound_update_topic,
                                double request_id, const char *uuid, const char *device_id)
{
  if (!url || url[0] == '\0')
  {
    OSI_LOGE(TAG, "[SOUND-UPDATE] url missing");
    if (is_sound_update_topic)
    {
      customer_send_language_ack(request_id, uuid, (device_id && device_id[0]) ? device_id : sn,
                                 "Failed", "url missing");
    }
    return -1;
  }

  if (g_sound_update_ctx.state != TEMPLATE_SOUND_STATE_IDLE)
  {
    OSI_LOGW(TAG, "[SOUND-UPDATE] request ignored, update already running");
    if (is_sound_update_topic)
    {
      customer_send_language_ack(request_id, uuid, (device_id && device_id[0]) ? device_id : sn,
                                 "Failed", "update already running");
    }
    return -2;
  }

  template_sound_cleanup_files();
  template_sound_reset_ctx();

  g_sound_update_ctx.is_sound_update_topic = is_sound_update_topic;
  if (request_id > 0)
  {
    g_sound_update_ctx.request_id = request_id;
  }
  if (uuid && uuid[0] != '\0')
  {
    strncpy(g_sound_update_ctx.uuid, uuid, sizeof(g_sound_update_ctx.uuid) - 1);
  }
  if (device_id && device_id[0] != '\0')
  {
    strncpy(g_sound_update_ctx.device_id, device_id, sizeof(g_sound_update_ctx.device_id) - 1);
  }
  else if (sn[0] != '\0')
  {
    strncpy(g_sound_update_ctx.device_id, sn, sizeof(g_sound_update_ctx.device_id) - 1);
  }

  if (checksum && checksum[0] != '\0')
  {
    strncpy(g_sound_update_ctx.checksum, checksum,
            sizeof(g_sound_update_ctx.checksum) - 1);
  }
  if (dst && dst[0] != '\0' && !strstr(dst, ".zip") && !strstr(dst, ".ZIP"))
  {
    strncpy(g_sound_update_ctx.unzip_dest, dst,
            sizeof(g_sound_update_ctx.unzip_dest) - 1);
  }
  else
  {
    // Default destination is D:/FEMALE/
    strncpy(g_sound_update_ctx.unzip_dest, "D:/FEMALE/",
            sizeof(g_sound_update_ctx.unzip_dest) - 1);
  }
  size_t dlen = strlen(g_sound_update_ctx.unzip_dest);
  if (dlen > 0 && g_sound_update_ctx.unzip_dest[dlen - 1] != '/' && g_sound_update_ctx.unzip_dest[dlen - 1] != '\\')
  {
    if (dlen < sizeof(g_sound_update_ctx.unzip_dest) - 1)
    {
      g_sound_update_ctx.unzip_dest[dlen] = '/';
      g_sound_update_ctx.unzip_dest[dlen + 1] = '\0';
    }
  }

  if (lang && lang[0] != '\0')
  {
    strncpy(g_sound_update_ctx.pending_lang, lang,
            sizeof(g_sound_update_ctx.pending_lang) - 1);
    g_sound_update_ctx.pending_lang[sizeof(g_sound_update_ctx.pending_lang) - 1] = '\0';
    OSI_LOGI(TAG, "[SOUND-UPDATE] Pending language: %s (will apply after successful unzip)", g_sound_update_ctx.pending_lang);
  }
  g_sound_update_ctx.state = TEMPLATE_SOUND_STATE_DOWNLOADING;

  int ret = framework_http_download_file(url, TEMPLATE_SOUND_ZIP_PATH,
                                         HTTP_DOWNLOAD_TYPE_RES_OTA);
  if (ret != 0)
  {
    OSI_LOGE(TAG, "[SOUND-UPDATE] failed to queue download (%d)", ret);
    template_sound_update_fail("failed to queue download");
    return ret;
  }

  OSI_LOGI(TAG, "[SOUND-UPDATE] download started url=%s dst=%s lang=%s req_id=%.0f uuid=%s dev_id=%s",
           url, g_sound_update_ctx.unzip_dest, g_sound_update_ctx.pending_lang,
           g_sound_update_ctx.request_id, g_sound_update_ctx.uuid, g_sound_update_ctx.device_id);
  return 0;
}

static void template_handle_http_download_msg(framework_msg_t *msg)
{
  if (!msg)
  {
    return;
  }

  http_download_type_t type = (http_download_type_t)msg->param1;
  if (type != HTTP_DOWNLOAD_TYPE_RES_OTA)
  {
    // FW FOTA already process in message handler
    return;
  }

  switch (msg->id)
  {
  case MSG_HTTP_DOWNLOAD_START:
    OSI_LOGI(TAG, "[SOUND-UPDATE] download start %d%%", msg->param2);
    break;
  case MSG_HTTP_DOWNLOAD_PROGRESS:
    OSI_LOGI(TAG, "[SOUND-UPDATE] download progress %d%%", msg->param2);
    break;
  case MSG_HTTP_DOWNLOAD_SUCCESS:
  {
    if (g_sound_update_ctx.state != TEMPLATE_SOUND_STATE_DOWNLOADING)
    {
      break;
    }
    const char *zip_path = (const char *)(uintptr_t)msg->param3;
    if (!zip_path || zip_path[0] == '\0')
    {
      zip_path = TEMPLATE_SOUND_ZIP_PATH;
    }

    uint32_t fsize = fs_size(zip_path);
    OSI_LOGI(TAG, "[SOUND-UPDATE] Download complete: %s (size: %u bytes)", zip_path, fsize);

    if (g_sound_update_ctx.checksum[0] != '\0')
    {
      char md5_actual[33] = {0};
      if (template_calc_file_md5(zip_path, md5_actual) != 0 ||
          template_compare_checksum(g_sound_update_ctx.checksum, md5_actual) !=
              0)
      {
        template_sound_update_fail("checksum mismatch");
        return;
      }
      OSI_LOGI(TAG, "[SOUND-UPDATE] checksum verified (%s)", md5_actual);
    }

    const char *unzip_dest = (g_sound_update_ctx.unzip_dest[0] != '\0')
                                 ? g_sound_update_ctx.unzip_dest
                                 : "D:/FEMALE/";
    int unzip_ret = framework_unzip_start(zip_path, unzip_dest,
                                          FRAMEWORK_UNZIP_TYPE_RESOURCES);
    if (unzip_ret != 0)
    {
      OSI_LOGE(TAG, "[SOUND-UPDATE] failed to start unzip (%d)", unzip_ret);
      template_sound_update_fail("unzip queue failed");
      return;
    }
    g_sound_update_ctx.state = TEMPLATE_SOUND_STATE_UNZIPPING;
    break;
  }
  case MSG_HTTP_DOWNLOAD_FAIL:
    OSI_LOGE(TAG, "[SOUND-UPDATE] Download failed (progress=%d%%, type=%d)", msg->param2, msg->param1);
    template_sound_update_fail("download failed");
    break;
  default:
    break;
  }
}

static void template_handle_unzip_msg(framework_msg_t *msg)
{
  if (!msg)
  {
    return;
  }

  framework_unzip_type_t type = (framework_unzip_type_t)msg->param1;
  if (type != FRAMEWORK_UNZIP_TYPE_RESOURCES)
  {
    return;
  }

  switch (msg->id)
  {
  case MSG_UNZIP_START:
    OSI_LOGI(TAG, "[SOUND-UPDATE] unzip started");
    break;
  case MSG_UNZIP_PROGRESS:
    OSI_LOGI(TAG, "[SOUND-UPDATE] unzip progress %d%%", msg->param2);
    break;
  case MSG_UNZIP_SUCCESS:
    if (g_sound_update_ctx.state == TEMPLATE_SOUND_STATE_UNZIPPING)
    {
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
                                          uint32_t max_len, uint32_t *out_len)
{
  if (!path || !buffer || max_len < 2)
  {
    return -1;
  }

  int fd = fs_open(path, "rb");
  if (fd < 0)
  {
    OSI_LOGE(TAG, "[CERT] open failed: %s", path);
    return -2;
  }

  uint32_t total = 0;
  while (total < (max_len - 1))
  {
    int read_len = fs_read(buffer + total, 1, (int)(max_len - 1 - total), fd);
    if (read_len <= 0)
    {
      break;
    }
    total += (uint32_t)read_len;
  }
  fs_close(fd);

  if (total == 0)
  {
    OSI_LOGE(TAG, "[CERT] file empty: %s", path);
    return -3;
  }

  if (total >= (max_len - 1))
  {
    OSI_LOGE(TAG, "[CERT] file too large: %s (max %lu)", path,
             (unsigned long)(max_len - 1));
    return -4;
  }

  buffer[total] = '\0';
  if (out_len)
  {
    *out_len = total;
  }
  return 0;
}

static int template_load_certificates_from_files(void)
{
  if (g_template_cert_loaded)
  {
    return 0;
  }

  if (template_load_file_into_buffer(
          TEMPLATE_CA_CERT_PATH, g_template_ca_cert_buf,
          sizeof(g_template_ca_cert_buf), &g_template_ca_cert_len) != 0)
  {
    return -1;
  }

  if (template_load_file_into_buffer(TEMPLATE_CLIENT_CERT_PATH,
                                     g_template_client_cert_buf,
                                     sizeof(g_template_client_cert_buf),
                                     &g_template_client_cert_len) != 0)
  {
    return -2;
  }

  if (template_load_file_into_buffer(
          TEMPLATE_CLIENT_KEY_PATH, g_template_client_key_buf,
          sizeof(g_template_client_key_buf), &g_template_client_key_len) != 0)
  {
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
                                           uint32_t payload_len)
{
  char buf[512];
  if (payload_len >= sizeof(buf))
    payload_len = sizeof(buf) - 1;
  memcpy(buf, payload, payload_len);
  buf[payload_len] = 0;

  // Handle pipe-delimited payment messages:
  // Format: Language|Mode of payment|Amount|...|Transaction Id
  // e.g. "EN|UPI|1590.00| | |415857764147"
  if (strchr(buf, '|'))
  {
    char *tokens[10] = {0};
    int token_count = 0;
    char temp_buf[512];
    strncpy(temp_buf, buf, sizeof(temp_buf) - 1);
    temp_buf[sizeof(temp_buf) - 1] = '\0';

    char *p = temp_buf;
    while (p && token_count < 10)
    {
      tokens[token_count++] = p;
      char *next = strchr(p, '|');
      if (next)
      {
        *next = '\0';
        p = next + 1;
      }
      else
      {
        break;
      }
    }

    for (int i = 0; i < token_count; i++)
    {
      tokens[i] = prv_str_trim(tokens[i]);
    }

    if (token_count >= 3)
    {
      // const char *lang_str = (token_count > 0 && tokens[0][0] != '\0') ? tokens[0] : s_default_language;
      const char *lang_str = customer_get_default_language();
      const char *mode = (token_count > 1) ? tokens[1] : "";
      const char *amount_str = (token_count > 2) ? tokens[2] : "0.00";

      // Find transaction id from remaining tokens (e.g. token[5] or token[3])
      const char *transaction_id = "";
      for (int i = token_count - 1; i >= 3; i--)
      {
        if (tokens[i][0] != '\0')
        {
          transaction_id = tokens[i];
          break;
        }
      }

      double amount_val = atof(amount_str);
      uint64_t rupees = (uint64_t)amount_val;
      uint32_t paise = (uint32_t)((amount_val - (double)rupees) * 100.0 + 0.5);
      if (paise >= 100)
      {
        rupees += 1;
        paise = 0;
      }

      OSI_LOGI(TAG,
               "[PIPE-PAYMENT] lang=%s, mode=%s, amount=%s (val=%.2f, rupees=%llu, paise=%lu), txn_id=%s",
               lang_str, mode, amount_str, amount_val,
               (unsigned long long)rupees, (unsigned long)paise, transaction_id);

      payment_broadcast_params_t params = {0};
      params.amount = rupees;
      params.rupees = rupees;
      params.paise = paise;
      params.total_amount = amount_val;
      strncpy(params.transaction_id, transaction_id, sizeof(params.transaction_id) - 1);
      strncpy(params.amount_str, amount_str, sizeof(params.amount_str) - 1);
      strncpy(params.language, lang_str, sizeof(params.language) - 1);

      int ret = customer_payment_broadcast(&params);
      if (ret != 0)
      {
        payment_callback_t callback = {0};
        strncpy(callback.serial_number, sn, sizeof(callback.serial_number) - 1);
        strncpy(callback.status, "0", sizeof(callback.status) - 1);
        strncpy(callback.transaction_id, transaction_id, sizeof(callback.transaction_id) - 1);
        strncpy(callback.amount, amount_str, sizeof(callback.amount) - 1);
        strncpy(callback.language, lang_str, sizeof(callback.language) - 1);
        strncpy(callback.reason, "Broadcast failed", sizeof(callback.reason) - 1);
        osiTime_t now = {0};
        osiGetTime(&now);
        snprintf(callback.sound_played_datetime, sizeof(callback.sound_played_datetime),
                 "%04d-%02d-%02d %02d:%02d:%02d",
                 now.tm_year, now.tm_mon, now.tm_mday,
                 now.tm_hour, now.tm_min, now.tm_sec);
        customer_status_report_payment(&callback);
      }
      return;
    }
  }

  cJSON *root = cJSON_Parse(buf);
  if (!root)
  {
    OSI_LOGI(TAG, "[JSON] Parse error: %s", buf);
    return;
  }

  if (strstr(topic, "/payment") || strstr(topic, "pebble") ||
      strstr(topic, "pebble_screen") || strstr(topic, "pocket") || strstr(topic, "neo"))
  {
    // Support both Hitachi TMS ("TxnId", "Amount", "Language") and legacy ("transaction_id", "amount", "language")
    const cJSON *j_id = cJSON_GetObjectItem(root, "transaction_id");
    if (!j_id)
    {
      j_id = cJSON_GetObjectItem(root, "TxnId");
    }
    const cJSON *j_amount = cJSON_GetObjectItem(root, "amount");
    if (!j_amount)
    {
      j_amount = cJSON_GetObjectItem(root, "Amount");
    }
    const cJSON *j_status = cJSON_GetObjectItem(root, "status");
    const cJSON *j_lang = cJSON_GetObjectItem(root, "language");
    if (!j_lang)
    {
      j_lang = cJSON_GetObjectItem(root, "Language");
    }
    if (!j_lang)
    {
      j_lang = cJSON_GetObjectItem(root, "lang");
    }
    if (!j_lang)
    {
      j_lang = cJSON_GetObjectItem(root, "Lang");
    }
    const char *transaction_id =
        j_id && cJSON_IsString(j_id) ? j_id->valuestring : "";

    char amount_str[16] = {0};
    double amount_val = 0.0;
    if (j_amount)
    {
      if (cJSON_IsNumber(j_amount))
      {
        amount_val = j_amount->valuedouble;
        snprintf(amount_str, sizeof(amount_str), "%.2f", amount_val);
      }
      else if (cJSON_IsString(j_amount) && j_amount->valuestring)
      {
        strncpy(amount_str, j_amount->valuestring, sizeof(amount_str) - 1);
        amount_val = atof(amount_str);
      }
    }
    uint64_t rupees = (uint64_t)amount_val;
    uint32_t paise = (uint32_t)((amount_val - (double)rupees) * 100.0 + 0.5);
    if (paise >= 100)
    {
      rupees += 1;
      paise = 0;
    }

    const char *lang_str = (j_lang && cJSON_IsString(j_lang)) ? j_lang->valuestring : NULL;
    if (!lang_str || lang_str[0] == '\0')
    {
      lang_str = customer_get_default_language();
    }

    const char *status =
        j_status && cJSON_IsString(j_status) ? j_status->valuestring : "";
    OSI_LOGI(TAG,
             "[PAYMENT] id=%s, amount=%s (val=%.2f, rupees=%llu, paise=%lu), lang=%s, status=%s",
             transaction_id, amount_str, amount_val, (unsigned long long)rupees,
             (unsigned long)paise, lang_str, status); // add by vipin
    payment_broadcast_params_t params = {0};
    params.amount = rupees;           // add by vipin: legacy integer field compatibility
    params.rupees = rupees;           // add by vipin: integer rupee portion
    params.paise = paise;             // add by vipin: integer paise portion (0-99)
    params.total_amount = amount_val; // add by vipin: full amount float/double
    strncpy(params.transaction_id, transaction_id, sizeof(params.transaction_id) - 1);
    strncpy(params.amount_str, amount_str, sizeof(params.amount_str) - 1);
    strncpy(params.language, lang_str, sizeof(params.language) - 1);
    int ret = customer_payment_broadcast(&params);
    if (ret != 0)
    {
      payment_callback_t callback = {0};
      strncpy(callback.serial_number, sn, sizeof(callback.serial_number) - 1);
      strncpy(callback.status, "0", sizeof(callback.status) - 1); // 0 = Failure
      strncpy(callback.transaction_id, transaction_id, sizeof(callback.transaction_id) - 1);
      strncpy(callback.amount, amount_str, sizeof(callback.amount) - 1);
      strncpy(callback.language, lang_str, sizeof(callback.language) - 1);
      strncpy(callback.reason, "Broadcast failed", sizeof(callback.reason) - 1);
      osiTime_t now = {0};
      osiGetTime(&now);
      snprintf(callback.sound_played_datetime, sizeof(callback.sound_played_datetime), "%04d-%02d-%02d %02d:%02d:%02d",
               now.tm_year, now.tm_mon, now.tm_mday,
               now.tm_hour, now.tm_min, now.tm_sec);
      customer_status_report_payment(&callback);
    }
    // Note: On success (ret == 0), transaction ACK is sent automatically
    // when audio finishes playing via prv_payment_audio_playback_callback
  }
  else if (strstr(topic, "/sound-update"))
  {
    const cJSON *j_url = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *j_md5 = cJSON_GetObjectItemCaseSensitive(root, "MD5");
    const cJSON *j_dst = cJSON_GetObjectItem(root, "dst");
    const cJSON *j_lang = cJSON_GetObjectItem(root, "language");
    if (!j_lang)
      j_lang = cJSON_GetObjectItem(root, "Language");
    if (!j_lang)
      j_lang = cJSON_GetObjectItem(root, "lang");
    if (!j_lang)
      j_lang = cJSON_GetObjectItem(root, "Lang");

    const cJSON *j_req_id = cJSON_GetObjectItem(root, "request_id");
    if (!j_req_id)
      j_req_id = cJSON_GetObjectItem(root, "requestId");
    if (!j_req_id)
      j_req_id = cJSON_GetObjectItem(root, "RequestId");

    const cJSON *j_uuid = cJSON_GetObjectItem(root, "uuid");
    if (!j_uuid)
      j_uuid = cJSON_GetObjectItem(root, "UUID");
    if (!j_uuid)
      j_uuid = cJSON_GetObjectItem(root, "Uuid");

    const cJSON *j_dev_id = cJSON_GetObjectItem(root, "deviceId");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "device_id");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "DeviceId");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "serial_no");

    double req_id = 0;
    if (j_req_id)
    {
      if (cJSON_IsNumber(j_req_id))
      {
        req_id = j_req_id->valuedouble;
      }
      else if (cJSON_IsString(j_req_id) && j_req_id->valuestring)
      {
        req_id = strtod(j_req_id->valuestring, NULL);
      }
    }
    const char *uuid_str = (j_uuid && cJSON_IsString(j_uuid)) ? j_uuid->valuestring : NULL;
    const char *dev_id_str = (j_dev_id && cJSON_IsString(j_dev_id)) ? j_dev_id->valuestring : NULL;

    const char *url =
        (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : NULL;
    const char *md5 =
        (j_md5 && cJSON_IsString(j_md5)) ? j_md5->valuestring : NULL;
    const char *dst =
        (j_dst && cJSON_IsString(j_dst)) ? j_dst->valuestring : NULL;
    const char *req_lang =
        (j_lang && cJSON_IsString(j_lang)) ? j_lang->valuestring : NULL;

    OSI_LOGI(TAG, "[SOUND-UPDATE] url=%s md5=%s dst=%s req_lang=%s req_id=%.0f uuid=%s dev_id=%s",
             url ? url : "(null)", md5 ? md5 : "(none)", dst ? dst : "(default: D:/FEMALE/)",
             req_lang ? req_lang : "(none)", req_id, uuid_str ? uuid_str : "(none)", dev_id_str ? dev_id_str : "(none)");
    if (template_start_sound_update(url, md5, dst, req_lang, true, req_id, uuid_str, dev_id_str) != 0)
    {
      OSI_LOGE(TAG, "[SOUND-UPDATE] request rejected");
    }
  }
  else if (strstr(topic, "/ota-update"))
  {
    // {"url":"...", "type":"..."}
    const cJSON *j_url = cJSON_GetObjectItem(root, "url");
    const cJSON *j_type = cJSON_GetObjectItem(root, "type");
    const char *url = (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : "";
    const char *type = (j_type && cJSON_IsString(j_type)) ? j_type->valuestring : NULL;

    customer_app_update_start(url, type);
  }
  else if (strstr(topic, "/restart"))
  {
    // {"restart":"true"}
    const cJSON *j_restart = cJSON_GetObjectItem(root, "restart");
    const char *restart =
        j_restart && cJSON_IsString(j_restart) ? j_restart->valuestring : "";
    OSI_LOGI(TAG, "[RESTART] restart=%s", restart);
    if (strcmp(restart, "true") == 0)
    {
      customer_dux_publish_device_power(0);
      osiSysReset();
    }
  }
  else if (strstr(topic, "/update-lang"))
  {
    const cJSON *j_lang = cJSON_GetObjectItem(root, "lang_support");
    if (!j_lang)
      j_lang = cJSON_GetObjectItem(root, "language");
    if (!j_lang)
      j_lang = cJSON_GetObjectItem(root, "Language");
    if (!j_lang)
      j_lang = cJSON_GetObjectItem(root, "lang");
    if (!j_lang)
      j_lang = cJSON_GetObjectItem(root, "active_language");
    const char *lang =
        j_lang && cJSON_IsString(j_lang) ? j_lang->valuestring : "";
    OSI_LOGI(TAG, "[LANG-UPDATE] lang_support=%s", lang);
    if (lang && lang[0] != '\0')
    {
      customer_set_default_language(lang);
    }
    OSI_LOGI(TAG, "[LANG-UPDATE] active language set to: %s (folder: %s)",
             customer_get_default_language(), soundbox_get_current_audio_folder());

    // Extract fields or fallback to hardcoded sample values
    const cJSON *j_req_id = cJSON_GetObjectItem(root, "request_id");
    if (!j_req_id)
      j_req_id = cJSON_GetObjectItem(root, "requestId");
    if (!j_req_id)
      j_req_id = cJSON_GetObjectItem(root, "RequestId");

    const cJSON *j_dev_id = cJSON_GetObjectItem(root, "deviceId");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "device_id");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "DeviceId");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "serial_no");

    const cJSON *j_act_lang = cJSON_GetObjectItem(root, "active_language");
    if (!j_act_lang)
      j_act_lang = cJSON_GetObjectItem(root, "active_lang");
    if (!j_act_lang)
      j_act_lang = j_lang;

    double req_id = 0;
    if (j_req_id)
    {
      if (cJSON_IsNumber(j_req_id))
      {
        req_id = j_req_id->valuedouble;
      }
      else if (cJSON_IsString(j_req_id) && j_req_id->valuestring)
      {
        req_id = strtod(j_req_id->valuestring, NULL);
      }
    }
    const char *dev_id_str = (j_dev_id && cJSON_IsString(j_dev_id)) ? j_dev_id->valuestring : NULL;
    const char *act_lang_str = (j_act_lang && cJSON_IsString(j_act_lang)) ? j_act_lang->valuestring : NULL;

    customer_send_change_language_ack(req_id,
                                      dev_id_str,
                                      " Success ",
                                      "",
                                      act_lang_str);
  }
  else if (strstr(topic, "/activation"))
  {
    // {"status":"SUCCESS"}
    const cJSON *j_status = cJSON_GetObjectItem(root, "status");
    const char *status =
        j_status && cJSON_IsString(j_status) ? j_status->valuestring : "";
    OSI_LOGI(TAG, "[ACTIVATION] status=%s", status);
    audio_resource_play_combo_async_mid_priority(
        PAYMENT_AUDIO_FOLDER, "activation-device.mp3", NULL, 1);
  }
  else if (strstr(topic, "audio-download"))
  {
    customer_audio_download_from_json(root);
  }
  else
  {
    OSI_LOGI(TAG, "Received message on unknown topic: %s", topic);
  }
  cJSON_Delete(root);
}

// MQTT message callback
static int template_mqtt_message_handler(const char *topic, const char *payload,
                                         uint32_t payload_len)
{
  OSI_LOGI(TAG, "MQTT message: topic=%s, payload=%.*s", topic, payload_len,
           payload);
  template_handle_server_message(topic, payload, payload_len);
  return 0; // Mark as handled
}

// MQTT event callback
static void template_mqtt_event_callback(mqtt_event_e event, void *data)
{
  if (event == MQTT_EVENT_CONNECTED)
  {
    // OSI_LOGI(TAG, "MQTT connected, subscribing to topics [%d]...",
    //          sizeof(g_template_topics) / sizeof(g_template_topics[0]));
    // for (int i = 0;
    //      i < sizeof(g_template_topics) / sizeof(g_template_topics[0]); ++i) {
    //   sprintf(sub_topic[i], g_template_topics[i], sn);
    int ret = framework_mqtt_subscribe(sn, 1);
    OSI_LOGI(TAG, "Subscribe %s: %d", sn, ret);

    // Start status reporting after MQTT connection is established (1 min interval)
    OSI_LOGI(TAG, "Starting status report (interval: 1 min / 60 sec)");
    customer_status_report_start(15 * 60);
  }
  else if (event == MQTT_EVENT_DISCONNECTED)
  {
    OSI_LOGW(TAG, "MQTT disconnected, stopping status report");
    // customer_status_report_stop();
  }
}

OSI_UNUSED static int customer_mqtt_message_handler(const char *topic, const char *payload,
                                                    uint32_t payload_len)
{
  if (!payload || payload_len == 0)
    return -1;
  OSI_LOGI(TAG, "Received command: %.*s", (int)payload_len, payload);
  return 0;
}

static void template_mqtt_reconnect_handler(void *data)
{
  (void)data;
  OSI_LOGI(TAG,
           "MQTT reconnect handler called, is_connected=%d",
           framework_network_is_connected());
  while (!framework_network_is_connected())
  {
    osiThreadSleep(200);
  }
}

static int customer_mqtt_config_handler(mqtt_config_t *config)
{
  if (!config)
  {
    return -1;
  }

  if (mqtt_reg.ip[0] != '\0') {
    snprintf(config->ip, sizeof(config->ip), "%s", mqtt_reg.ip);
  } else {
    snprintf(config->ip, sizeof(config->ip), "%s", "1.7.140.58");
  }
  config->host = config->ip;
  config->port = (mqtt_reg.port > 0) ? mqtt_reg.port : 9000;

  config->client_id = sn;
  config->mqtt_version = 4; // MQTT 3.1.1
  config->keepalive_sec = 120;
  config->clean_session = 1;
  config->default_qos = 1;
  config->enable_ssl = 0;   // Plain TCP as per server endpoint & MQTTX
  config->cert = NULL;
  config->sni_host = NULL;

  if (mqtt_reg.username[0] != '\0') {
    config->username = mqtt_reg.username;
    config->password = mqtt_reg.password;
    config->auth_type = MQTT_AUTH_USERNAME_PASSWORD;
  } else {
    config->username = NULL;
    config->password = NULL;
    config->auth_type = MQTT_AUTH_NONE;
  }

  OSI_LOGI(TAG, "[MQTT-CONFIG] Host=%s, Port=%d, ClientID=%s, User=%s, SSL=%d",
           config->host, config->port, config->client_id,
           config->username ? config->username : "<none>", config->enable_ssl);
  return 0;
}

int customer_mqtt_init(void)
{
  customer_language_init();
  if (g_mqtt_initialized)
  {
    return 0;
  }

  // Initialize status report module
  if (customer_status_report_init() != 0)
  {
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
  if (mqtt_reg.ip[0] != '\0') {
    snprintf(config.ip, sizeof(config.ip), "%s", mqtt_reg.ip);
  } else {
    snprintf(config.ip, sizeof(config.ip), "%s", "1.7.140.58");
  }
  config.host = config.ip;
  config.port = (mqtt_reg.port > 0) ? mqtt_reg.port : 9000;
  config.client_id = sn;
  config.mqtt_version = 4;
  config.keepalive_sec = 120;
  config.clean_session = 1;
  config.default_qos = 1;
  config.enable_ssl = 0;
  if (mqtt_reg.username[0] != '\0') {
    config.username = mqtt_reg.username;
    config.password = mqtt_reg.password;
    config.auth_type = MQTT_AUTH_USERNAME_PASSWORD;
  } else {
    config.auth_type = MQTT_AUTH_NONE;
  }
  reconnect_count_idx = 0;

  if (framework_mqtt_init(&config) != 0)
  {
    OSI_LOGI(TAG, "Failed to initialize framework MQTT");
    return -1;
  }
  if (framework_mqtt_start() != 0)
  {
    OSI_LOGI(TAG, "Failed to start MQTT connection");
    return -1;
  }
  OSI_LOGI(TAG, "template MQTT handler initialized successfully");
  g_mqtt_initialized = 1;
  return 0;
}

int customer_mqtt_network_ready(void)
{
  OSI_LOGI(TAG, "Network ready notification received");
  return 0;
}

void template_mqtt_handle_http_msg(framework_msg_t *msg)
{
  template_handle_http_download_msg(msg);
}

void template_mqtt_handle_unzip_msg(framework_msg_t *msg)
{
  template_handle_unzip_msg(msg);
}
