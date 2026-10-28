/**
 * @file customer_campaign.c
 * @brief TMS Campaign MP3 Download and Post-Transaction Playback Implementation
 */

#include "customer_campaign.h"
#include "customer_register.h"
#include "framework_audio.h"
#include "framework_http.h"
#include "framework_msg.h"
#include "framework_log.h"
#include "fs_api.h"
#include "osi_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define TAG "CAMPAIGN"

#define CAMPAIGN_MAX_VAL_LEN 256
#define CAMPAIGN_MAX_URL_LEN 256

static char s_active_campaign_name[64] = {0};
static char s_pending_target_name[64] = {0};
static bool s_campaign_downloading = false;
static osiMutex_t *s_campaign_mutex = NULL;

/* ============================================================================
 * Helper Utilities
 * ============================================================================ */

static void prv_campaign_lock(void)
{
    if (!s_campaign_mutex)
    {
        s_campaign_mutex = osiMutexCreate();
    }
    if (s_campaign_mutex)
    {
        osiMutexLock(s_campaign_mutex);
    }
}

static void prv_campaign_unlock(void)
{
    if (s_campaign_mutex)
    {
        osiMutexUnlock(s_campaign_mutex);
    }
}

static char *prv_trim(char *str)
{
    if (!str) return NULL;
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
    {
        *end = '\0';
        end--;
    }
    return str;
}

static int prv_file_copy(const char *src, const char *dst)
{
    int in_fd = fs_open(src, "rb");
    if (in_fd < 0) return -1;
    int out_fd = fs_open(dst, "wb+");
    if (out_fd < 0)
    {
        fs_close(in_fd);
        return -2;
    }

    char buf[512];
    int bytes_read = 0;
    while ((bytes_read = fs_read(buf, 1, sizeof(buf), in_fd)) > 0)
    {
        fs_write((void *)buf, 1, bytes_read, out_fd);
    }

    fs_close(in_fd);
    fs_close(out_fd);
    return 0;
}

/**
 * @brief Validate MP3 file by checking size and audio header signatures
 */
static bool prv_validate_mp3_file(const char *path)
{
    if (!path || path[0] == '\0')
    {
        return false;
    }

    uint32_t sz = fs_size(path);
    if (sz == 0 || sz < 64)
    {
        return false;
    }

    int fd = fs_open(path, "rb");
    if (fd < 0)
    {
        return false;
    }

    uint8_t hdr[16] = {0};
    int r = fs_read((void *)hdr, 1, sizeof(hdr), fd);
    fs_close(fd);

    if (r < 4)
    {
        return false;
    }

    /* Check ID3 tag ("ID3") */
    if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3')
    {
        return true;
    }

    /* Check MPEG audio syncword (0xFF followed by high 3 bits set in second byte) */
    if (hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0)
    {
        return true;
    }

    /* Check RIFF/WAV header ("RIFF") */
    if (hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F')
    {
        return true;
    }

    /* Valid non-empty audio data file */
    return (sz >= 128);
}

/**
 * @brief Validate host against approved trusted host allowlist
 */
static bool prv_is_trusted_host(const char *host)
{
    if (!host || host[0] == '\0')
    {
        return false;
    }

    /* 1. Standard TMS IP/domain */
    if (strcmp(host, "1.7.140.58") == 0 || strncmp(host, "1.7.140.58:", 11) == 0)
    {
        return true;
    }

    /* 2. Base URL host configured in customer_register */
    const char *base_url = customer_register_get_base_url();
    if (base_url && strstr(base_url, host) != NULL)
    {
        return true;
    }

    /* 3. Approved test host */
    if (strcmp(host, "klgbefvjgjtcwcsyfwwc.supabase.co") == 0)
    {
        return true;
    }

    return false;
}

/* ============================================================================
 * HTTP Download Callback
 * ============================================================================ */

static void prv_campaign_download_cb(uint32_t msg_id, const char *save_path, void *user_data)
{
    (void)user_data;
    prv_campaign_lock();
    s_campaign_downloading = false;

    if (msg_id == MSG_HTTP_DOWNLOAD_SUCCESS && fs_size(CAMPAIGN_TEMP_FILE_PATH) > 0 &&
        prv_validate_mp3_file(CAMPAIGN_TEMP_FILE_PATH))
    {
        OSI_LOGI(TAG, "[CAMPAIGN] Download and validation successful");

        fs_remove(CAMPAIGN_ACTIVE_FILE_PATH);
        int ren = fs_rename(CAMPAIGN_TEMP_FILE_PATH, CAMPAIGN_ACTIVE_FILE_PATH);
        if (ren != 0)
        {
            prv_file_copy(CAMPAIGN_TEMP_FILE_PATH, CAMPAIGN_ACTIVE_FILE_PATH);
            fs_remove(CAMPAIGN_TEMP_FILE_PATH);
        }

        strncpy(s_active_campaign_name, s_pending_target_name, sizeof(s_active_campaign_name) - 1);
        s_active_campaign_name[sizeof(s_active_campaign_name) - 1] = '\0';
        OSI_LOGI(TAG, "[CAMPAIGN] Activated new campaign file: %s (%s)",
                 s_active_campaign_name, CAMPAIGN_ACTIVE_FILE_PATH);
    }
    else
    {
        OSI_LOGE(TAG, "[CAMPAIGN] Invalid input or download failure");
        fs_remove(CAMPAIGN_TEMP_FILE_PATH);
        /* Preserve existing active campaign on failure */
    }

    prv_campaign_unlock();
}

/* ============================================================================
 * Public APIs
 * ============================================================================ */

void customer_campaign_init(void)
{
    prv_campaign_lock();
    if (fs_size(CAMPAIGN_ACTIVE_FILE_PATH) > 0)
    {
        OSI_LOGI(TAG, "[CAMPAIGN] Active campaign file present on disk (%u bytes)",
                 (unsigned int)fs_size(CAMPAIGN_ACTIVE_FILE_PATH));
    }
    prv_campaign_unlock();
}

bool customer_campaign_has_active(void)
{
    return (fs_size(CAMPAIGN_ACTIVE_FILE_PATH) > 0);
}

const char *customer_campaign_get_active_name(void)
{
    return s_active_campaign_name;
}

int customer_campaign_on_transaction_completed(void)
{
    if (fs_size(CAMPAIGN_ACTIVE_FILE_PATH) > 0)
    {
        OSI_LOGI(TAG, "[CAMPAIGN] Starting post-transaction playback");
        return audio_resource_play_combo_async_mid_priority("", CAMPAIGN_ACTIVE_FILE_PATH, NULL, 0);
    }
    return 0;
}

int customer_campaign_process_field(const char *field4)
{
    OSI_LOGI(TAG, "[CAMPAIGN] Raw campaign field received");

    if (!field4)
    {
        return 0;
    }

    size_t in_len = strlen(field4);
    if (in_len == 0)
    {
        return 0;
    }
    if (in_len >= CAMPAIGN_MAX_VAL_LEN)
    {
        OSI_LOGE(TAG, "[CAMPAIGN] Invalid input or download failure");
        return -1;
    }

    char campaign_value[CAMPAIGN_MAX_VAL_LEN] = {0};
    strncpy(campaign_value, field4, sizeof(campaign_value) - 1);
    char *trimmed = prv_trim(campaign_value);

    if (!trimmed || trimmed[0] == '\0')
    {
        /* Empty fourth field - reject/skip safely without disrupting transaction */
        return 0;
    }

    OSI_LOGI(TAG, "[CAMPAIGN] Normalized campaign value: %s", trimmed);

    char basename[64] = {0};
    char direct_url[CAMPAIGN_MAX_URL_LEN] = {0};
    bool is_direct_url = false;
    const char *colon_slash = strstr(trimmed, "://");

    if (colon_slash != NULL)
    {
        /* Case B: URL-like value */
        char scheme[16] = {0};
        size_t scheme_len = colon_slash - trimmed;
        if (scheme_len >= sizeof(scheme)) scheme_len = sizeof(scheme) - 1;
        strncpy(scheme, trimmed, scheme_len);
        scheme[scheme_len] = '\0';

        /* Validate scheme: only http and https are supported */
        if (strcasecmp(scheme, "http") != 0 && strcasecmp(scheme, "https") != 0)
        {
            OSI_LOGE(TAG, "[CAMPAIGN] Invalid input or download failure");
            return -2;
        }

        OSI_LOGI(TAG, "[CAMPAIGN] Input classified as URL-like value");

        const char *after_scheme = colon_slash + 3;
        const char *first_slash = strchr(after_scheme, '/');

        if (first_slash == NULL)
        {
            /* Filename wrapper without path (e.g. "https://dewali.mp3") */
            strncpy(basename, after_scheme, sizeof(basename) - 1);
        }
        else
        {
            /* Full URL with host and path */
            char host[64] = {0};
            size_t host_len = first_slash - after_scheme;
            if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
            strncpy(host, after_scheme, host_len);
            host[host_len] = '\0';

            /* Validate host against trusted allowlist */
            if (!prv_is_trusted_host(host))
            {
                OSI_LOGE(TAG, "[CAMPAIGN] Invalid input or download failure");
                return -3;
            }

            const char *last_slash = strrchr(after_scheme, '/');
            if (last_slash && *(last_slash + 1) != '\0')
            {
                strncpy(basename, last_slash + 1, sizeof(basename) - 1);
                strncpy(direct_url, trimmed, sizeof(direct_url) - 1);
                is_direct_url = true;
            }
            else
            {
                OSI_LOGE(TAG, "[CAMPAIGN] Invalid input or download failure");
                return -4;
            }
        }
    }
    else
    {
        /* Case A: Filename */
        OSI_LOGI(TAG, "[CAMPAIGN] Input classified as filename");
        strncpy(basename, trimmed, sizeof(basename) - 1);
    }

    /* Validate audio file extension */
    size_t blen = strlen(basename);
    if (blen < 4 || (strcasecmp(basename + blen - 4, ".mp3") != 0 && strcasecmp(basename + blen - 4, ".wav") != 0))
    {
        OSI_LOGE(TAG, "[CAMPAIGN] Invalid input or download failure");
        return -5;
    }

    prv_campaign_lock();

    /* Avoid downloading the same valid campaign repeatedly */
    if (strcmp(s_active_campaign_name, basename) == 0 && fs_size(CAMPAIGN_ACTIVE_FILE_PATH) > 0)
    {
        prv_campaign_unlock();
        return 0;
    }

    /* Prevent concurrent downloads from corrupting active campaign */
    if (s_campaign_downloading)
    {
        prv_campaign_unlock();
        OSI_LOGW(TAG, "[CAMPAIGN] Download already in progress, skipping duplicate trigger");
        return 0;
    }

    s_campaign_downloading = true;
    strncpy(s_pending_target_name, basename, sizeof(s_pending_target_name) - 1);
    s_pending_target_name[sizeof(s_pending_target_name) - 1] = '\0';
    prv_campaign_unlock();

    char download_url[CAMPAIGN_MAX_URL_LEN] = {0};

    if (is_direct_url && direct_url[0] != '\0')
    {
        strncpy(download_url, direct_url, sizeof(download_url) - 1);
    }
    else
    {
        /* Construct standard TMS Campaign Download API URL */
        const char *base_url = customer_register_get_base_url();
        const char *partner_id = customer_register_get_partner_id();
        const char *app_code = customer_register_get_app_code();

        char clean_base[128] = {0};
        strncpy(clean_base, base_url ? base_url : "https://1.7.140.58/pos", sizeof(clean_base) - 1);
        size_t cblen = strlen(clean_base);
        if (cblen > 0 && clean_base[cblen - 1] == '/')
        {
            clean_base[cblen - 1] = '\0';
        }

        snprintf(download_url, sizeof(download_url), "%s/TMS/SoundFile/DownloadMP3File/%s_%s_%s",
                 clean_base,
                 (partner_id && partner_id[0]) ? partner_id : "b15eeee6-b841-44af-9448-2c58d2cadb11",
                 (app_code && app_code[0]) ? app_code : "HPRCEKSPFI",
                 basename);
    }

    OSI_LOGI(TAG, "[CAMPAIGN] Starting TMS campaign download");
    OSI_LOGI(TAG, "[CAMPAIGN] Target download URL: %s", download_url);

    int dl_ret = framework_http_download_file_callback(
        download_url,
        CAMPAIGN_TEMP_FILE_PATH,
        HTTP_DOWNLOAD_TYPE_FILE,
        prv_campaign_download_cb,
        NULL
    );

    if (dl_ret != 0)
    {
        prv_campaign_lock();
        s_campaign_downloading = false;
        prv_campaign_unlock();
        OSI_LOGE(TAG, "[CAMPAIGN] Invalid input or download failure");
        return -6;
    }

    return 0;
}
