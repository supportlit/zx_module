/**
 * @file customer_payment_handler.c
 * @brief template 支付播报处理器
 */

#include "customer_payment_handler.h"
#include "framework_log.h"
#include "framework_audio.h"
#include "audio_resource_map.h"
#include "app_lcd.h"
#include "fs_api.h" // add by vipin: for filesystem checks
#include "customer_status_report.h"
#include "framework_device_config.h"
#include "osi_api.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_AUDIO_LIST_SIZE 2048

static int prv_add_audio_name(char *buf, size_t buf_size, const char *filename) {
    size_t current_len = strlen(buf);
    size_t filename_len = strlen(filename);
    if (current_len + filename_len + 2 > buf_size) {
        FRAMEWORK_LOG("Audio list buffer full");
        return -1;
    }
    strcat(buf, filename);
    strcat(buf, ",");
    return 0;
}

// add by vipin: helper to check and append currency audio (checks both id_ prefixed and plain name)
static int prv_add_currency_audio(char *buf, size_t buf_size, const char *preferred, const char *fallback) {
    char full_path[128];
    const char *prefix = audio_resource_get_language_prefix();
    if (!prefix) prefix = "D:/FEMALE/";

    if (preferred) {
        snprintf(full_path, sizeof(full_path), "%s%s", prefix, preferred);
        if (fs_size(full_path) > 0) {
            return prv_add_audio_name(buf, buf_size, preferred);
        }
    }
    if (fallback) {
        snprintf(full_path, sizeof(full_path), "%s%s", prefix, fallback);
        if (fs_size(full_path) > 0) {
            return prv_add_audio_name(buf, buf_size, fallback);
        }
    }
    // Default fallback to preferred if fs_size check is unavailable
    if (preferred) {
        return prv_add_audio_name(buf, buf_size, preferred);
    }
    return 0;
}

// add by vipin: Generate 1~999 number sequence (e.g. 150 -> 1 hundred 50)
static int prv_units_to_seq(int n, char *buf, size_t buf_size) {
    int ret = 0;
    if (n <= 0) return 0;
    int hundreds = n / 100;
    int rem = n % 100;
    if (hundreds) {
        // add by vipin: For 100, play "id_1.mp3" + "id_hundred.mp3" (e.g. 1 hundred 50 instead of seratus/100)
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "id_%d.mp3", hundreds);
        ret = prv_add_audio_name(buf, buf_size, tmp);
        if (ret) return ret;
        ret = prv_add_audio_name(buf, buf_size, "id_hundred.mp3");
        if (ret) return ret;
    }
    if (rem) {
        if (rem < 10) {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "id_%d.mp3", rem);
            ret = prv_add_audio_name(buf, buf_size, tmp);
            if (ret) return ret;
        } else if (rem < 20) {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "id_%d.mp3", rem);
            ret = prv_add_audio_name(buf, buf_size, tmp);
            if (ret) return ret;
        } else {
            int tens = (rem / 10) * 10;
            int unit = rem % 10;
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "id_%d.mp3", tens);
            ret = prv_add_audio_name(buf, buf_size, tmp);
            if (ret) return ret;
            if (unit) {
                snprintf(tmp, sizeof(tmp), "id_%d.mp3", unit);
                ret = prv_add_audio_name(buf, buf_size, tmp);
                if (ret) return ret;
            }
        }
    }
    return 0;
}

// add by vipin: Sequence generator for full number
static int prv_number_to_sequence(uint64_t n, char *buf, size_t buf_size) {
    int ret = 0;
    if (n <= 0) {
        return 0;
    }
    // Millions
    uint64_t millions = n / 1000000;
    if (millions) {
        if (millions == 1) {
            ret = prv_add_audio_name(buf, buf_size, "id_1.mp3");
            if (ret) return ret;
            ret = prv_add_currency_audio(buf, buf_size, "id_million.mp3", "million.mp3");
            if (ret) return ret;
        } else {
            ret = prv_units_to_seq((int)millions, buf, buf_size);
            if (ret) return ret;
            ret = prv_add_currency_audio(buf, buf_size, "id_million.mp3", "million.mp3");
            if (ret) return ret;
        }
    }
    n = n % 1000000;
    // Thousands
    uint64_t thousands = n / 1000;
    if (thousands) {
        // add by vipin: For 1000, play "id_1.mp3" + "id_thousand.mp3" instead of seribu
        if (thousands == 1) {
            ret = prv_add_audio_name(buf, buf_size, "id_1.mp3");
            if (ret) return ret;
            ret = prv_add_audio_name(buf, buf_size, "id_thousand.mp3");
            if (ret) return ret;
        } else {
            ret = prv_units_to_seq((int)thousands, buf, buf_size);
            if (ret) return ret;
            ret = prv_add_audio_name(buf, buf_size, "id_thousand.mp3");
            if (ret) return ret;
        }
    }
    n = n % 1000;
    // Hundred / tens / units
    if (n > 0) {
        ret = prv_units_to_seq((int)n, buf, buf_size);
        if (ret) return ret;
    }
    return 0;
}

// add by vipin: generate audio list with rupees/rupee and paise handling
static int prv_generate_amount_audio_list(const payment_broadcast_params_t *params, char *audio_list, size_t list_size)
{
    int ret = 0;
    audio_list[0] = '\0';
    // 1. id_received.mp3
    ret = prv_add_audio_name(audio_list, list_size, "id_received.mp3");
    if (ret) return ret;

    uint64_t rupees = params->rupees; // add by vipin
    uint32_t paise = params->paise;   // add by vipin

    // Legacy fallback: if rupees and paise both 0 but amount > 0
    if (rupees == 0 && paise == 0 && params->amount > 0) {
        rupees = params->amount;
    }

    // 2. Rupees handling (add by vipin)
    if (rupees > 0) {
        ret = prv_number_to_sequence(rupees, audio_list, list_size);
        if (ret) return ret;

        if (rupees == 1) {
            // add by vipin: Singular - 1 Rupee
            ret = prv_add_currency_audio(audio_list, list_size, "id_rupee.mp3", "rupee.mp3");
            if (ret) return ret;
        } else {
            // add by vipin: Plural - Rupees
            ret = prv_add_currency_audio(audio_list, list_size, "id_rupees.mp3", "rupees.mp3");
            if (ret) return ret;
        }
    }

    // 3. Paise handling (add by vipin: e.g. 50 paise)
    if (paise > 0) {
        ret = prv_units_to_seq((int)paise, audio_list, list_size);
        if (ret) return ret;

        if (paise == 1) {
            // add by vipin: Singular - 1 Paisa
            ret = prv_add_currency_audio(audio_list, list_size, "id_paisa.mp3", "paisa.mp3");
            if (ret) return ret;
        } else {
            // add by vipin: Plural - Paise
            ret = prv_add_currency_audio(audio_list, list_size, "id_paise.mp3", "paise.mp3");
            if (ret) return ret;
        }
    }

    // If both rupees and paise are 0 (amount = 0)
    if (rupees == 0 && paise == 0) {
        char path[128];
        const char *prefix = audio_resource_get_language_prefix();
        if (!prefix) prefix = "D:/FEMALE/";
        snprintf(path, sizeof(path), "%sid_0.mp3", prefix);
        if (fs_size(path) > 0) {
            ret = prv_add_audio_name(audio_list, list_size, "id_0.mp3");
            if (ret == 0) {
                prv_add_currency_audio(audio_list, list_size, "id_rupees.mp3", "rupees.mp3");
            }
        }
    }

    // 4. id_thank_you.mp3
    ret = prv_add_audio_name(audio_list, list_size, "id_thank_you.mp3");
    if (ret) return ret;
    return 0;
}

static payment_broadcast_params_t s_last_payment_params = {0};
static bool s_has_last_payment = false;
static uint32_t s_total_txn_count = 0; // add by vipin: total transaction counter
static uint32_t s_last_txn_count = 0;  // add by vipin: transaction count of last payment

#ifndef CUSTOMER_LCD_SHOW_TXN_COUNT
#define CUSTOMER_LCD_SHOW_TXN_COUNT 1 // add by vipin: 1 = Show Total Txn Count on LCD side display, 0 = Hide side count
#endif

static char s_pending_txn_id[64] = {0};
static char s_pending_amount_str[16] = {0};
static char s_pending_language[8] = {0};

static void prv_payment_audio_playback_callback(audio_resource_id_t res_id, audio_playback_status_t status, int result)
{
    if (status == AUDIO_PLAYBACK_STATUS_STARTED) {
        FRAMEWORK_LOG("[PAYMENT_AUDIO] Playback started on speaker");
    } else if (status == AUDIO_PLAYBACK_STATUS_ENDED) {
        FRAMEWORK_LOG("[PAYMENT_AUDIO] Playback completed successfully");
        if (s_pending_txn_id[0] != '\0') {
            payment_callback_t cb = {0};
            char sn[32] = {0};
            framework_device_config_get_sn(sn, sizeof(sn));
            strncpy(cb.serial_number, sn, sizeof(cb.serial_number) - 1);
            strncpy(cb.status, "1", sizeof(cb.status) - 1); // 1 = Success
            strncpy(cb.transaction_id, s_pending_txn_id, sizeof(cb.transaction_id) - 1);
            strncpy(cb.amount, s_pending_amount_str, sizeof(cb.amount) - 1);
            strncpy(cb.language, s_pending_language, sizeof(cb.language) - 1);
            cb.reason[0] = '\0';
            osiTime_t now = {0};
            osiGetTime(&now);
            snprintf(cb.sound_played_datetime, sizeof(cb.sound_played_datetime), "%04d-%02d-%02d %02d:%02d:%02d",
                     now.tm_year, now.tm_mon, now.tm_mday,
                     now.tm_hour, now.tm_min, now.tm_sec);
            customer_status_report_payment(&cb);
            s_pending_txn_id[0] = '\0';
            s_pending_amount_str[0] = '\0';
            s_pending_language[0] = '\0';
        }
    } else if (status == AUDIO_PLAYBACK_STATUS_ERROR) {
        FRAMEWORK_LOG("[PAYMENT_AUDIO] Playback error: %d", result);
        if (s_pending_txn_id[0] != '\0') {
            payment_callback_t cb = {0};
            char sn[32] = {0};
            framework_device_config_get_sn(sn, sizeof(sn));
            strncpy(cb.serial_number, sn, sizeof(cb.serial_number) - 1);
            strncpy(cb.status, "0", sizeof(cb.status) - 1); // 0 = Failure
            strncpy(cb.transaction_id, s_pending_txn_id, sizeof(cb.transaction_id) - 1);
            strncpy(cb.amount, s_pending_amount_str, sizeof(cb.amount) - 1);
            strncpy(cb.language, s_pending_language, sizeof(cb.language) - 1);
            snprintf(cb.reason, sizeof(cb.reason), "Audio play error %d", result);
            osiTime_t now = {0};
            osiGetTime(&now);
            snprintf(cb.sound_played_datetime, sizeof(cb.sound_played_datetime), "%04d-%02d-%02d %02d:%02d:%02d",
                     now.tm_year, now.tm_mon, now.tm_mday,
                     now.tm_hour, now.tm_min, now.tm_sec);
            customer_status_report_payment(&cb);
            s_pending_txn_id[0] = '\0';
            s_pending_amount_str[0] = '\0';
            s_pending_language[0] = '\0';
        }
    }
}

// add by vipin: internal broadcast handler distinguishing new transaction vs replay
static int prv_customer_payment_broadcast_internal(const payment_broadcast_params_t *params, bool is_replay)
{
    int ret = 0;
    char *audio_list = NULL;
    if (params == NULL) {
        FRAMEWORK_LOG("Invalid payment params");
        return -1;
    }
    FRAMEWORK_LOG("Payment broadcast: rupees=%llu, paise=%lu, total=%.2f, is_replay=%d", 
                  (unsigned long long)params->rupees, (unsigned long)params->paise, params->total_amount, is_replay); // add by vipin

    if (!is_replay) {
        s_total_txn_count++; // add by vipin: increment transaction count for new payment
        s_last_txn_count = s_total_txn_count;
        if (params->transaction_id[0] != '\0') {
            strncpy(s_pending_txn_id, params->transaction_id, sizeof(s_pending_txn_id) - 1);
            s_pending_txn_id[sizeof(s_pending_txn_id) - 1] = '\0';
        }
        if (params->amount_str[0] != '\0') {
            strncpy(s_pending_amount_str, params->amount_str, sizeof(s_pending_amount_str) - 1);
            s_pending_amount_str[sizeof(s_pending_amount_str) - 1] = '\0';
        } else {
            snprintf(s_pending_amount_str, sizeof(s_pending_amount_str), "%.2f", params->total_amount);
        }
        if (params->language[0] != '\0') {
            strncpy(s_pending_language, params->language, sizeof(s_pending_language) - 1);
            s_pending_language[sizeof(s_pending_language) - 1] = '\0';
        } else {
            strncpy(s_pending_language, "EN", sizeof(s_pending_language) - 1);
            s_pending_language[sizeof(s_pending_language) - 1] = '\0';
        }
    }

    audio_list = (char *)malloc(MAX_AUDIO_LIST_SIZE);
    if (!audio_list) {
        FRAMEWORK_LOG("Failed to allocate audio list buffer");
        return -1;
    }
    ret = prv_generate_amount_audio_list(params, audio_list, MAX_AUDIO_LIST_SIZE);
    if (ret != 0) {
        FRAMEWORK_LOG("Failed to generate audio list");
        free(audio_list);
        return -1;
    }
    size_t len = strlen(audio_list);
    if (len > 0 && audio_list[len - 1] == ',') {
        audio_list[len - 1] = '\0';
        len--;
    }
    if (len == 0) {
        FRAMEWORK_LOG("No audio entries generated for payment");
        free(audio_list);
        return -1;
    }
    FRAMEWORK_LOG("Playing combo payment audio: folder=%s, list=%s", PAYMENT_AUDIO_FOLDER, audio_list);
#if defined(CUSTOMER_ENABLE_LCD) && (CUSTOMER_ENABLE_LCD == 1)
    // add by vipin: Display amount with Rs icon, tick, decimal and total transaction count on LCD
    lcdParam_t lcd = {0};
    lcd.lcd_hook = true;      // Success tick
    lcd.lcd_rs = true;        // "Rs." icon
    lcd.lcd_num = params->rupees > 0 ? params->rupees : params->amount;
    lcd.lcd_decimalnum = params->paise;
#if (CUSTOMER_LCD_SHOW_TXN_COUNT == 1)
    lcd.lcd_count = true;     // add by vipin: enable COUNT icon on LCD
    lcd.lcd_amount = (s_last_txn_count > 0) ? s_last_txn_count : s_total_txn_count; // add by vipin: show 1, 2, 3...
#else
    lcd.lcd_count = false;    // add by vipin: disable COUNT icon
    lcd.lcd_amount = 0;       // add by vipin: clear side digits
#endif
    appLcdset(lcd);
#else
    prvLcdSecCodeSet((long)(params->rupees > 0 ? params->rupees : params->amount));
#endif
    ret = audio_resource_play_combo_async_mid_priority(PAYMENT_AUDIO_FOLDER, audio_list, prv_payment_audio_playback_callback, 0);
    free(audio_list);
    if (ret != 0) {
        FRAMEWORK_LOG("Failed to play payment combo (err=%d)", ret);
        return -1;
    }
    if (!is_replay) {
        s_last_payment_params = *params;
        s_has_last_payment = true;
    }
    FRAMEWORK_LOG("Successfully broadcast payment (txn_count=%u)", (unsigned int)s_last_txn_count);
    return 0;
}

int customer_payment_broadcast(const payment_broadcast_params_t *params)
{
    return prv_customer_payment_broadcast_internal(params, false); // add by vipin
}

int customer_payment_replay_last(void)
{
    if (!s_has_last_payment) {
        FRAMEWORK_LOG("No previous payment transaction to replay");
        return -1;
    }
    FRAMEWORK_LOG("Replaying last payment: rupees=%llu, paise=%lu (txn_count=%u)", 
                  (unsigned long long)s_last_payment_params.rupees, (unsigned long)s_last_payment_params.paise, (unsigned int)s_last_txn_count); // add by vipin
    return prv_customer_payment_broadcast_internal(&s_last_payment_params, true); // add by vipin: replay without incrementing count
}

// add by vipin: get total transactions count
uint32_t customer_payment_get_total_count(void)
{
    return s_total_txn_count;
}

// add by vipin: reset total transactions count
void customer_payment_reset_total_count(void)
{
    s_total_txn_count = 0;
    s_last_txn_count = 0;
    FRAMEWORK_LOG("Transaction count reset to 0");
}
