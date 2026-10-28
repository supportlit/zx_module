#include "customer_mqtt_handler.h"
#include "soundbox_language.h"
#include "cJSON.h"
#include "customer_config.h"
#include "customer_language_ack.h"
#include "customer_change_language_ack.h"
#include "customer_payment_handler.h"
#include "customer_campaign.h"
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
#include "TmsExceptionDownload.h"
#include "customer_lang_download.h"
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

static int prv_split_pipe_tokens(char *buf, char *tokens[], int max_tokens)
{
  int count = 0;
  char *curr = buf;
  while (curr && count < max_tokens)
  {
    tokens[count++] = curr;
    char *sep = strchr(curr, '|');
    if (sep)
    {
      *sep = '\0';
      curr = sep + 1;
    }
    else
    {
      break;
    }
  }
  return count;
}

static void template_handle_server_message(const char *topic,
                                           const char *payload,
                                           uint32_t payload_len)
{
  /* 1. Print the received MQTT topic before processing the payload. */
  if (!topic || !payload || payload_len == 0)
  {
    OSI_LOGE(TAG, "[MQTT] Invalid message: topic or payload is NULL/empty");
    return;
  }
  OSI_LOGI(TAG, "[MQTT] Received topic: %s", topic);

  /* 2. Safely copy and validate incoming payload in a bounded buffer. */
  char buf[512];
  if (payload_len >= sizeof(buf))
  {
    payload_len = sizeof(buf) - 1;
  }
  memcpy(buf, payload, payload_len);
  buf[payload_len] = '\0';

  /* 3. Identify the operation from the MQTT topic and dispatch. */

  /* =========================================================================
   * OPERATION 1: sound-update
   * Input format: url|language_code|destination_folder [|md5|req_id|uuid|dev_id]
   * ========================================================================= */
  if (strstr(topic, "sound-update") || strstr(buf, "sound-update|") == buf || strncmp(buf, "sound-update |", 14) == 0)
  {
    cJSON *root = NULL;
    if (buf[0] == '{')
    {
      root = cJSON_Parse(buf);
    }
    else
    {
      char *tokens[10] = {0};
      int count = prv_split_pipe_tokens(buf, tokens, 10);
      for (int i = 0; i < count; i++)
      {
        tokens[i] = prv_str_trim(tokens[i]);
      }
      root = cJSON_CreateObject();
      if (root)
      {
        int t_idx = 0;
        if (count > 1 && (strcasecmp(tokens[0], "sound-update") == 0 || strcasecmp(tokens[0], "sound_update") == 0))
        {
          t_idx = 1;
        }
        if (count > t_idx && tokens[t_idx][0] != '\0')
        {
          cJSON_AddStringToObject(root, "url", tokens[t_idx]);
        }
        if (count > t_idx + 1 && tokens[t_idx + 1][0] != '\0')
        {
          cJSON_AddStringToObject(root, "language", tokens[t_idx + 1]);
        }
        if (count > t_idx + 2 && tokens[t_idx + 2][0] != '\0')
        {
          cJSON_AddStringToObject(root, "dst", tokens[t_idx + 2]);
        }
        if (count > t_idx + 3 && tokens[t_idx + 3][0] != '\0')
        {
          cJSON_AddStringToObject(root, "MD5", tokens[t_idx + 3]);
        }
        if (count > t_idx + 4 && tokens[t_idx + 4][0] != '\0')
        {
          double req_id = strtod(tokens[t_idx + 4], NULL);
          if (req_id > 0)
          {
            cJSON_AddNumberToObject(root, "request_id", req_id);
          }
        }
        if (count > t_idx + 5 && tokens[t_idx + 5][0] != '\0')
        {
          cJSON_AddStringToObject(root, "uuid", tokens[t_idx + 5]);
        }
        if (count > t_idx + 6 && tokens[t_idx + 6][0] != '\0')
        {
          cJSON_AddStringToObject(root, "deviceId", tokens[t_idx + 6]);
        }
      }
    }

    if (!root)
    {
      OSI_LOGE(TAG, "[SOUND-UPDATE] Failed to create or parse JSON object");
      return;
    }

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

    const char *url = (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : NULL;
    const char *md5 = (j_md5 && cJSON_IsString(j_md5)) ? j_md5->valuestring : NULL;
    const char *dst = (j_dst && cJSON_IsString(j_dst)) ? j_dst->valuestring : NULL;
    const char *req_lang = (j_lang && cJSON_IsString(j_lang)) ? j_lang->valuestring : NULL;

    if (!url || url[0] == '\0')
    {
      OSI_LOGE(TAG, "[SOUND-UPDATE] Missing or invalid URL");
    }
    else
    {
      OSI_LOGI(TAG, "[SOUND-UPDATE] url=%s md5=%s dst=%s req_lang=%s req_id=%.0f uuid=%s dev_id=%s",
               url, md5 ? md5 : "(none)", dst ? dst : "(default: D:/FEMALE/)",
               req_lang ? req_lang : "(none)", req_id, uuid_str ? uuid_str : "(none)", dev_id_str ? dev_id_str : "(none)");
      if (template_start_sound_update(
              url,
              md5,
              dst,
              req_lang,
              true,
              req_id,
              uuid_str,
              dev_id_str) != 0)
      {
        OSI_LOGE(TAG, "[SOUND-UPDATE] request rejected");
      }
      else
      {
        OSI_LOGI(TAG, "[SOUND-UPDATE] sound update initiated successfully");
      }
    }
    cJSON_Delete(root);
  }
  /* =========================================================================
   * OPERATION 2: audio-download
   * Input format: url|destination_file [|language_code]
   * ========================================================================= */
  else if (strstr(topic, "audio-download") || strstr(buf, "audio-download|") == buf || strncmp(buf, "audio-download |", 16) == 0)
  {
    cJSON *root = NULL;
    if (buf[0] == '{')
    {
      root = cJSON_Parse(buf);
    }
    else
    {
      char *tokens[5] = {0};
      int count = prv_split_pipe_tokens(buf, tokens, 5);
      for (int i = 0; i < count; i++)
      {
        tokens[i] = prv_str_trim(tokens[i]);
      }
      root = cJSON_CreateObject();
      if (root)
      {
        int t_idx = 0;
        if (count > 1 && (strcasecmp(tokens[0], "audio-download") == 0 || strcasecmp(tokens[0], "audio_download") == 0))
        {
          t_idx = 1;
        }
        if (count > t_idx && tokens[t_idx][0] != '\0')
        {
          cJSON_AddStringToObject(root, "url", tokens[t_idx]);
        }
        if (count > t_idx + 1 && tokens[t_idx + 1][0] != '\0')
        {
          cJSON_AddStringToObject(root, "dst", tokens[t_idx + 1]);
        }
        if (count > t_idx + 2 && tokens[t_idx + 2][0] != '\0')
        {
          cJSON_AddStringToObject(root, "language", tokens[t_idx + 2]);
        }
        cJSON_AddBoolToObject(root, "download", true);
      }
    }

    if (!root)
    {
      OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] Failed to create or parse JSON object");
      return;
    }

    OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Dispatching audio download request to handler");
    int ad_ret = customer_audio_download_from_json(root);
    if (ad_ret != 0)
    {
      OSI_LOGE(TAG, "[AUDIO-DOWNLOAD] Audio download handling failed: %d", ad_ret);
    }
    else
    {
      OSI_LOGI(TAG, "[AUDIO-DOWNLOAD] Audio download handling succeeded");
    }
    cJSON_Delete(root);
  }
  /* =========================================================================
   * OPERATION 3: restart
   * Input format: true (or true|...)
   * ========================================================================= */
  else if (strstr(topic, "restart") || strstr(buf, "restart|") == buf || strncmp(buf, "restart |", 9) == 0)
  {
    cJSON *root = NULL;
    if (buf[0] == '{')
    {
      root = cJSON_Parse(buf);
    }
    else
    {
      char *tokens[3] = {0};
      int count = prv_split_pipe_tokens(buf, tokens, 3);
      for (int i = 0; i < count; i++)
      {
        tokens[i] = prv_str_trim(tokens[i]);
      }
      root = cJSON_CreateObject();
      if (root)
      {
        int t_idx = 0;
        if (count > 1 && strcasecmp(tokens[0], "restart") == 0)
        {
          t_idx = 1;
        }
        const char *restart_val = (count > t_idx) ? tokens[t_idx] : "";
        cJSON_AddStringToObject(root, "restart", restart_val);
      }
    }

    if (!root)
    {
      OSI_LOGE(TAG, "[RESTART] Failed to create or parse JSON object");
      return;
    }

    const cJSON *j_restart = cJSON_GetObjectItem(root, "restart");
    const char *restart = (j_restart && cJSON_IsString(j_restart)) ? j_restart->valuestring : "";
    OSI_LOGI(TAG, "[RESTART] restart=%s", restart);
    if (strcmp(restart, "true") == 0)
    {
      OSI_LOGI(TAG, "[RESTART] Executing device reset");
      customer_dux_publish_device_power(0);
      osiSysReset();
    }
    else
    {
      OSI_LOGW(TAG, "[RESTART] Ignored: restart field missing or not 'true'");
    }
    cJSON_Delete(root);
  }
  /* =========================================================================
   * OPERATION 4: ota-update
   * Input format: ota-update | <url> | <sha256_hash> (Secure OTA)
   * Legacy format: url [|type] OR JSON {"url":"...", "type":"..."}
   * ========================================================================= */
  else if (strstr(topic, "ota-update") ||
           strstr(topic, "ota_update") ||
           strncmp(buf, "ota-update", 10) == 0 ||
           strncmp(buf, "ota_update", 10) == 0)
  {
    /* Check for pipe-separated secure OTA format (non-JSON) */
    if (buf[0] != '{' && strchr(buf, '|') != NULL)
    {
      int ota_ret = customer_app_update_handle_mqtt_command(buf);
      if (ota_ret == 0)
      {
        return;
      }
      /* If explicit ota-update command failed validation or was rejected, stop processing */
      if (strncmp(buf, "ota-update", 10) == 0 || strncmp(buf, "ota_update", 10) == 0)
      {
        return;
      }
    }

    cJSON *root = NULL;
    if (buf[0] == '{')
    {
      root = cJSON_Parse(buf);
    }
    else
    {
      char *tokens[3] = {0};
      int count = prv_split_pipe_tokens(buf, tokens, 3);
      for (int i = 0; i < count; i++)
      {
        tokens[i] = prv_str_trim(tokens[i]);
      }
      root = cJSON_CreateObject();
      if (root)
      {
        int t_idx = 0;
        if (count > 1 && (strcasecmp(tokens[0], "ota-update") == 0 || strcasecmp(tokens[0], "ota_update") == 0))
        {
          t_idx = 1;
        }
        if (count > t_idx && tokens[t_idx][0] != '\0')
        {
          cJSON_AddStringToObject(root, "url", tokens[t_idx]);
        }
        if (count > t_idx + 1 && tokens[t_idx + 1][0] != '\0')
        {
          cJSON_AddStringToObject(root, "type", tokens[t_idx + 1]);
        }
      }
    }

    if (!root)
    {
      OSI_LOGE(TAG, "[OTA-UPDATE] Failed to create or parse JSON object");
      return;
    }

    const cJSON *j_url = cJSON_GetObjectItem(root, "url");
    const cJSON *j_type = cJSON_GetObjectItem(root, "type");
    const char *url = (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : "";
    const char *type = (j_type && cJSON_IsString(j_type)) ? j_type->valuestring : NULL;

    if (url && url[0] != '\0')
    {
      OSI_LOGI(TAG, "[OTA-UPDATE] Starting OTA update: url=%s, type=%s", url, type ? type : "(default)");
      customer_app_update_start(url, type);
    }
    else
    {
      OSI_LOGE(TAG, "[OTA-UPDATE] Invalid or missing URL");
    }
    cJSON_Delete(root);
  }
  /* =========================================================================
   * OPERATION 5: update-lang / CHANGEDEFLANG
   * Input format:
   *   CHANGEDEFLANG|DSN|request_id|HI
   *   update-lang [|request_id|device_id]
   *   JSON: {"deviceId":"...", "request_id":..., "language":"..."}
   * ========================================================================= */
  else if (strstr(topic, "update-lang") ||
           strstr(topic, "CHANGEDEFLANG") ||
           strncmp(buf, "update-lang|", 12) == 0 ||
           strncmp(buf, "update-lang |", 13) == 0 ||
           strncmp(buf, "update_lang|", 12) == 0 ||
           strncmp(buf, "CHANGEDEFLANG|", 14) == 0 ||
           strncmp(buf, "CHANGEDEFLANG", 13) == 0)
  {
    cJSON *root = NULL;
    bool is_change_def_lang = false;

    if (buf[0] == '{')
    {
      /* Existing JSON payload support */
      root = cJSON_Parse(buf);
    }
    else
    {
      char *tokens[5] = {0};
      int count = prv_split_pipe_tokens(buf, tokens, 5);

      for (int i = 0; i < count; i++)
      {
        tokens[i] = prv_str_trim(tokens[i]);
      }

      if (count > 0 &&
          strcasecmp(tokens[0], "CHANGEDEFLANG") == 0)
      {
        is_change_def_lang = true;

        /*
         * Format:
         * CHANGEDEFLANG|DSN|request_id|HI
         *
         * tokens[0] = command
         * tokens[1] = DSN
         * tokens[2] = request_id
         * tokens[3] = language
         */
        if (count < 4 ||
            !tokens[1] || tokens[1][0] == '\0' ||
            !tokens[2] || tokens[2][0] == '\0' ||
            !tokens[3] || tokens[3][0] == '\0')
        {
          OSI_LOGE(TAG,
                   "[LANG-UPDATE] Invalid CHANGEDEFLANG payload");
          double err_req_id = (count > 2 && tokens[2] && tokens[2][0] != '\0') ? strtod(tokens[2], NULL) : 0;
          const char *err_dev = (count > 1 && tokens[1] && tokens[1][0] != '\0') ? tokens[1] : (sn[0] != '\0' ? sn : NULL);
          const char *curr_str = customer_get_default_language();
          customer_send_change_language_ack(
              err_req_id,
              err_dev,
              "Failed",
              "Invalid CHANGEDEFLANG payload",
              curr_str);
          return;
        }

        char *endptr = NULL;
        double parsed_req_id = strtod(tokens[2], &endptr);

        if (endptr == tokens[2] ||
            *endptr != '\0' ||
            parsed_req_id <= 0)
        {
          OSI_LOGE(TAG,
                   "[LANG-UPDATE] Invalid request_id: %s",
                   tokens[2]);
          const char *err_dev = (tokens[1] && tokens[1][0] != '\0') ? tokens[1] : (sn[0] != '\0' ? sn : NULL);
          const char *curr_str = customer_get_default_language();
          customer_send_change_language_ack(
              0,
              err_dev,
              "Failed",
              "Invalid request_id",
              curr_str);
          return;
        }

        root = cJSON_CreateObject();
        if (root)
        {
          cJSON_AddStringToObject(root, "deviceId", tokens[1]);
          cJSON_AddNumberToObject(root, "request_id", parsed_req_id);
          cJSON_AddStringToObject(root, "lang_support", tokens[3]);
          cJSON_AddStringToObject(root, "language", tokens[3]);
        }
      }
      else
      {
        /* Existing update-lang / update_lang pipe format */
        root = cJSON_CreateObject();

        if (root)
        {
          int t_idx = 0;

          if (count > 0 &&
              (strcasecmp(tokens[0], "update-lang") == 0 ||
               strcasecmp(tokens[0], "update_lang") == 0))
          {
            t_idx = 1;
          }

          const char *lang_code =
              (count > t_idx && tokens[t_idx])
                  ? tokens[t_idx]
                  : "";

          cJSON_AddStringToObject(root, "lang_support", lang_code);
          cJSON_AddStringToObject(root, "language", lang_code);

          if (count > t_idx + 1 &&
              tokens[t_idx + 1] &&
              tokens[t_idx + 1][0] != '\0')
          {
            char *endptr = NULL;
            double parsed_req_id =
                strtod(tokens[t_idx + 1], &endptr);

            if (endptr != tokens[t_idx + 1] &&
                *endptr == '\0' &&
                parsed_req_id > 0)
            {
              cJSON_AddNumberToObject(
                  root, "request_id", parsed_req_id);
            }
          }

          if (count > t_idx + 2 &&
              tokens[t_idx + 2] &&
              tokens[t_idx + 2][0] != '\0')
          {
            cJSON_AddStringToObject(
                root, "deviceId", tokens[t_idx + 2]);
          }
        }
      }
    }

    if (!root)
    {
      OSI_LOGE(TAG,
               "[LANG-UPDATE] Failed to create or parse JSON object");
      return;
    }

    /* Get requested language */
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
        (j_lang && cJSON_IsString(j_lang) && j_lang->valuestring)
            ? j_lang->valuestring
            : "";

    /* Get request ID */
    const cJSON *j_req_id = cJSON_GetObjectItem(root, "request_id");

    if (!j_req_id)
      j_req_id = cJSON_GetObjectItem(root, "requestId");
    if (!j_req_id)
      j_req_id = cJSON_GetObjectItem(root, "RequestId");

    double req_id = 0;

    if (j_req_id)
    {
      if (cJSON_IsNumber(j_req_id))
      {
        req_id = j_req_id->valuedouble;
      }
      else if (cJSON_IsString(j_req_id) &&
               j_req_id->valuestring)
      {
        char *endptr = NULL;
        req_id = strtod(j_req_id->valuestring, &endptr);

        if (endptr == j_req_id->valuestring || *endptr != '\0')
        {
          req_id = 0;
        }
      }
    }

    /* Get device ID / DSN */
    const cJSON *j_dev_id = cJSON_GetObjectItem(root, "deviceId");

    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "device_id");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "DeviceId");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "serial_no");
    if (!j_dev_id)
      j_dev_id = cJSON_GetObjectItem(root, "DSN");

    const char *dev_id_str =
        (j_dev_id && cJSON_IsString(j_dev_id) &&
         j_dev_id->valuestring && j_dev_id->valuestring[0] != '\0')
            ? j_dev_id->valuestring
            : (sn[0] != '\0' ? sn : NULL);

    if (lang[0] == '\0')
    {
      OSI_LOGE(TAG, "[LANG-UPDATE] Missing or empty language code");
      const char *curr_str = customer_get_default_language();
      customer_send_change_language_ack(
          req_id,
          dev_id_str,
          "Failed",
          "Missing or empty language code",
          curr_str);
      cJSON_Delete(root);
      return;
    }

    /* 1. Read current active language using persistent file */
    soundbox_language_t current_lang = SOUNDBOX_DEFAULT_LANGUAGE;
    int load_ret = soundbox_load_language(&current_lang);
    if (load_ret != 0)
    {
      OSI_LOGE(TAG, "[LANG-UPDATE] Cannot read current language configuration (err=%d)", load_ret);
      const char *curr_str = soundbox_language_to_str(soundbox_get_language());
      customer_send_change_language_ack(
          req_id,
          dev_id_str,
          "Failed",
          "Cannot read current language configuration",
          curr_str);
      cJSON_Delete(root);
      return;
    }

    const char *current_lang_str = soundbox_language_to_str(current_lang);

    /* Diagnostic logs per project specification */
    OSI_LOGI(TAG, "[LANG-UPDATE] Language change request received");
    OSI_LOGI(TAG, "[LANG-UPDATE] Requested language: %s", lang);
    OSI_LOGI(TAG, "[LANG-UPDATE] Current language: %s", current_lang_str);

    /* Parse requested language code */
    soundbox_language_t req_lang = SOUNDBOX_DEFAULT_LANGUAGE;
    bool is_supported = soundbox_str_to_language(lang, &req_lang);

    /* 2. Check Case A: Requested language is already active (idempotent) */
    if (is_supported && current_lang == req_lang)
    {
      OSI_LOGI(TAG, "[LANG-UPDATE] Requested language is already active (%s == %s). No change needed.",
               lang, current_lang_str);
      OSI_LOGI(TAG, "[LANG-UPDATE] Language change result: SUCCESS");

      const char *act_resp_str = isdigit((unsigned char)lang[0])
          ? soundbox_language_to_numeric_str(current_lang)
          : soundbox_language_to_str(current_lang);

      customer_send_change_language_ack(
          req_id,
          dev_id_str,
          " Success ",
          "",
          act_resp_str);

      cJSON_Delete(root);
      return;
    }

    /* 3. Check if requested language code is supported */
    if (!is_supported)
    {
      OSI_LOGE(TAG, "[LANG-UPDATE] Requested language code '%s' is not supported", lang);
      OSI_LOGI(TAG, "[LANG-UPDATE] Language change result: FAILED");

      const char *act_resp_str = isdigit((unsigned char)lang[0])
          ? soundbox_language_to_numeric_str(current_lang)
          : soundbox_language_to_str(current_lang);

      customer_send_change_language_ack(
          req_id,
          dev_id_str,
          "Failed",
          "Unsupported language code",
          act_resp_str);

      cJSON_Delete(root);
      return;
    }

    const char *folder = soundbox_get_audio_folder(req_lang);
    if (!folder || folder[0] == '\0')
    {
      OSI_LOGE(TAG, "[LANG-UPDATE] Language %s has no audio folder configured in firmware",
               soundbox_language_name(req_lang));
      OSI_LOGI(TAG, "[LANG-UPDATE] Language change result: FAILED");

      const char *act_resp_str = isdigit((unsigned char)lang[0])
          ? soundbox_language_to_numeric_str(current_lang)
          : soundbox_language_to_str(current_lang);

      customer_send_change_language_ack(
          req_id,
          dev_id_str,
          "Failed",
          "Language audio folder not configured",
          act_resp_str);

      cJSON_Delete(root);
      return;
    }

    /* 4. Validate requested language resources (welcome audio) */
    OSI_LOGI(TAG, "[LANG-UPDATE] Checking requested language resources");
    bool resource_exists = soundbox_validate_welcome_audio(req_lang);
    OSI_LOGI(TAG, "[LANG-UPDATE] Requested language resource validation: %s",
             resource_exists ? "PASS" : "FAIL");

    if (!resource_exists)
    {
      OSI_LOGE(TAG, "[LANG-UPDATE] Required welcome audio resource missing for %s in %s",
               soundbox_language_name(req_lang), folder);
      OSI_LOGI(TAG, "[LANG-UPDATE] Language change result: FAILED");

      const char *act_resp_str = isdigit((unsigned char)lang[0])
          ? soundbox_language_to_numeric_str(current_lang)
          : soundbox_language_to_str(current_lang);

      customer_send_change_language_ack(
          req_id,
          dev_id_str,
          "Failed",
          "Required welcome audio resource missing",
          act_resp_str);

      cJSON_Delete(root);
      return;
    }

    /* 5. Apply requested language using soundbox_set_language and verify */
    int set_ret = soundbox_set_language(req_lang, true);

    soundbox_language_t verify_lang = SOUNDBOX_DEFAULT_LANGUAGE;
    int verify_ret = soundbox_load_language(&verify_lang);

    bool change_success = (set_ret == 0 && verify_ret == 0 &&
                           verify_lang == req_lang &&
                           soundbox_get_language() == req_lang);

    OSI_LOGI(TAG, "[LANG-UPDATE] Language change result: %s",
             change_success ? "SUCCESS" : "FAILED");

    if (!change_success)
    {
      OSI_LOGE(TAG, "[LANG-UPDATE] Failed to apply or persist language (set_ret=%d, verify_ret=%d)",
               set_ret, verify_ret);
      /* Revert in-memory and persistent state to previous current_lang */
      soundbox_set_language(current_lang, true);

      const char *act_resp_str = isdigit((unsigned char)lang[0])
          ? soundbox_language_to_numeric_str(current_lang)
          : soundbox_language_to_str(current_lang);

      customer_send_change_language_ack(
          req_id,
          dev_id_str,
          "Failed",
          "Failed to apply language setting",
          act_resp_str);

      cJSON_Delete(root);
      return;
    }

    OSI_LOGI(TAG,
             "[LANG-UPDATE] active language set to: %s (folder: %s)",
             customer_get_default_language(),
             soundbox_get_current_audio_folder());

    const char *act_resp_str = isdigit((unsigned char)lang[0])
        ? soundbox_language_to_numeric_str(req_lang)
        : soundbox_language_to_str(req_lang);

    customer_send_change_language_ack(
        req_id,
        dev_id_str,
        " Success ",
        "",
        act_resp_str);

    cJSON_Delete(root);

    OSI_LOGI(TAG,
             "[LANG-UPDATE] Language command processed (%s)",
             is_change_def_lang ? "CHANGEDEFLANG" : "legacy");
  }
  /* =========================================================================
   * OPERATION 6: activation
   * ========================================================================= */
  else if (strstr(topic, "activation"))
  {
    OSI_LOGI(TAG, "[ACTIVATION] Processing activation prompt");
    audio_resource_play_combo_async_mid_priority(
        PAYMENT_AUDIO_FOLDER, "activation-device.mp3", NULL, 1);
  }
  else if (strncmp(buf, "DOWNLOADLANG|", 13) == 0)
  {
    OSI_LOGI(TAG, "[MQTT-ROUTER] Routing DOWNLOADLANG command: %s", buf);
    customer_lang_download_handle_mqtt_command(buf);
    return;
  }
  else if (strstr(topic, "audio-patch") ||
           strncmp(buf, "audio-patch|", 12) == 0)
  {
    char *tokens[3] = {0};

    int count = prv_split_pipe_tokens(buf, tokens, 3);

    for (int i = 0; i < count; i++)
    {
      tokens[i] = prv_str_trim(tokens[i]);
    }

    /*
     * Expected payload:
     * audio-patch|flag|url
     *
     * Example:
     * audio-patch|1|https://example.com/audio/
     */

    if (count < 3 ||
        tokens[1] == NULL || tokens[1][0] == '\0' ||
        tokens[2] == NULL || tokens[2][0] == '\0')
    {
      OSI_LOGE(TAG,
               "[AUDIO-PATCH] Invalid payload. Expected: audio-patch|flag|url");
    }
    else
    {
      char *endptr = NULL;
      long flag_value = strtol(tokens[1], &endptr, 10);

      if (endptr == tokens[1] ||
          *endptr != '\0' ||
          (flag_value != 0 && flag_value != 1))
      {
        OSI_LOGE(TAG,
                 "[AUDIO-PATCH] Invalid flag value: %s",
                 tokens[1]);
      }
      else
      {
        int flag = (int)flag_value;
        const char *url = tokens[2];
        const char *language = customer_get_default_language();

        OSI_LOGI(TAG,
                 "[AUDIO-PATCH] flag=%d, url=%s, language=%s",
                 flag,
                 url,
                 language ? language : "(null)");

        tms_exception_download_status_t exe_status =
            tms_exception_download_process(
                flag,
                url,
                language);

        OSI_LOGI(TAG,
                 "[AUDIO-PATCH] Exception download status=%d",
                 (int)exe_status);
      }
    }
  }
  /* =========================================================================
   * OPERATION 7: Payment messages (Pipe-delimited or JSON)
   * ========================================================================= */
  else
  {
    /* 1. Pipe-delimited Payment format:
     * Language|Mode of payment|Amount|...|Transaction Id
     * e.g. "EN|UPI|1590.00| | |415857764147"
     */
    if (strchr(buf, '|'))
    {
      char *tokens[10] = {0};
      int token_count = prv_split_pipe_tokens(buf, tokens, 10);
      for (int i = 0; i < token_count; i++)
      {
        tokens[i] = prv_str_trim(tokens[i]);
      }

      if (token_count >= 3)
      {
        const char *lang_str = (token_count > 0 && tokens[0][0] != '\0') ? tokens[0] : customer_get_default_language();
        const char *mode = (token_count > 1) ? tokens[1] : "";
        const char *amount_str = (token_count > 2) ? tokens[2] : "0.00";

        const char *raw_campaign = NULL;
        const char *transaction_id = "";

        if (token_count >= 6)
        {
          /* 6-field standard payload: Lang|Mode|Amount|Campaign|Reserved|TxnID */
          raw_campaign = tokens[3];
          transaction_id = (tokens[5][0] != '\0') ? tokens[5] : tokens[token_count - 1];
        }
        else if (token_count == 5)
        {
          if (strstr(tokens[3], ".mp3") || strstr(tokens[3], "http://") || strstr(tokens[3], "https://"))
          {
            raw_campaign = tokens[3];
            transaction_id = tokens[4];
          }
          else
          {
            transaction_id = tokens[4];
          }
        }
        else
        {
          for (int i = token_count - 1; i >= 3; i--)
          {
            if (tokens[i][0] != '\0')
            {
              transaction_id = tokens[i];
              break;
            }
          }
        }

        /* Process Field 4 Campaign if provided */
        if (raw_campaign != NULL && raw_campaign[0] != '\0')
        {
          customer_campaign_process_field(raw_campaign);
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

    /* 2. JSON-formatted payment format */
    if (buf[0] == '{')
    {
      cJSON *root = cJSON_Parse(buf);
      if (!root)
      {
        OSI_LOGE(TAG, "[JSON-PAYMENT] Parse error: %s", buf);
        return;
      }

      const cJSON *j_id = cJSON_GetObjectItem(root, "transaction_id");
      if (!j_id)
        j_id = cJSON_GetObjectItem(root, "TxnId");

      const cJSON *j_amount = cJSON_GetObjectItem(root, "amount");
      if (!j_amount)
        j_amount = cJSON_GetObjectItem(root, "Amount");

      const cJSON *j_status = cJSON_GetObjectItem(root, "status");

      const cJSON *j_lang = cJSON_GetObjectItem(root, "language");
      if (!j_lang)
        j_lang = cJSON_GetObjectItem(root, "Language");
      if (!j_lang)
        j_lang = cJSON_GetObjectItem(root, "lang");
      if (!j_lang)
        j_lang = cJSON_GetObjectItem(root, "Lang");

      const char *transaction_id = (j_id && cJSON_IsString(j_id)) ? j_id->valuestring : "";

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

      const cJSON *j_campaign = cJSON_GetObjectItem(root, "campaign");
      if (!j_campaign) j_campaign = cJSON_GetObjectItem(root, "sound_file");
      if (!j_campaign) j_campaign = cJSON_GetObjectItem(root, "campaign_url");
      if (j_campaign && cJSON_IsString(j_campaign) && j_campaign->valuestring && j_campaign->valuestring[0] != '\0')
      {
        customer_campaign_process_field(j_campaign->valuestring);
      }

      const char *status = (j_status && cJSON_IsString(j_status)) ? j_status->valuestring : "";
      OSI_LOGI(TAG,
               "[PAYMENT] id=%s, amount=%s (val=%.2f, rupees=%llu, paise=%lu), lang=%s, status=%s",
               transaction_id, amount_str, amount_val, (unsigned long long)rupees,
               (unsigned long)paise, lang_str, status);

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
      cJSON_Delete(root);
      return;
    }

    OSI_LOGW(TAG, "[MQTT] Received message on unknown topic or unhandled payload: topic=%s", topic);
  }
}

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

    if (mqtt_reg.partner_id[0] != '\0')
    {
      int ret_partner = framework_mqtt_subscribe(mqtt_reg.partner_id, 1);
      OSI_LOGI(TAG, "Subscribe PartnerId %s: %d", mqtt_reg.partner_id, ret_partner);
    }

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

  if (mqtt_reg.ip[0] != '\0')
  {
    snprintf(config->ip, sizeof(config->ip), "%s", mqtt_reg.ip);
  }
  else
  {
    snprintf(config->ip, sizeof(config->ip), "%s", "1.7.140.58");
  }
  config->host = config->ip;
  config->port = (mqtt_reg.port > 0) ? mqtt_reg.port : 9000;

  config->client_id = sn;
  config->mqtt_version = 4; // MQTT 3.1.1
  config->keepalive_sec = 120;
  config->clean_session = 1;
  config->default_qos = 1;
  config->enable_ssl = 0; // Plain TCP as per server endpoint & MQTTX
  config->cert = NULL;
  config->sni_host = NULL;

  if (mqtt_reg.username[0] != '\0')
  {
    config->username = mqtt_reg.username;
    config->password = mqtt_reg.password;
    config->auth_type = MQTT_AUTH_USERNAME_PASSWORD;
  }
  else
  {
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

  // Initialize TMS language pack download manager
  customer_lang_download_init();

  // Initialize secure OTA module
  customer_app_update_init();

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
  if (mqtt_reg.ip[0] != '\0')
  {
    snprintf(config.ip, sizeof(config.ip), "%s", mqtt_reg.ip);
  }
  else
  {
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
  if (mqtt_reg.username[0] != '\0')
  {
    config.username = mqtt_reg.username;
    config.password = mqtt_reg.password;
    config.auth_type = MQTT_AUTH_USERNAME_PASSWORD;
  }
  else
  {
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
  customer_campaign_init();
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
