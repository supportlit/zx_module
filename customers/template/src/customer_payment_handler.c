/**
 * @file customer_payment_handler.c
 * @brief template 支付播报处理器
 */

#include "customer_payment_handler.h"
#include "customer_campaign.h"
#include "soundbox_language.h"
#include "customer_mqtt_handler.h"
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

#define AUDIO_FOLDER_PATH soundbox_get_current_audio_folder()

static bool prv_is_lang_hindi(const char *lang);

// add by vipin: helper to check file existence with language variations in D:/FEMALE/
static bool prv_try_file_with_lang(char *buf, size_t buf_size, const char *folder, const char *fname, const char *lang) {
    char full_path[128];
    if (lang && lang[0] != '\0') {
        char base[64] = {0};
        strncpy(base, fname, sizeof(base) - 1);
        char *ext = strrchr(base, '.');
        if (ext) *ext = '\0';

        char cand[64];
        char lang_upper[16] = {0};
        char lang_lower[16] = {0};
        for (int i = 0; lang[i] && i < (int)sizeof(lang_upper) - 1; i++) {
            lang_upper[i] = (lang[i] >= 'a' && lang[i] <= 'z') ? (lang[i] - ('a' - 'A')) : lang[i];
            lang_lower[i] = (lang[i] >= 'A' && lang[i] <= 'Z') ? (lang[i] + ('a' - 'A')) : lang[i];
        }

        // 1. Suffix: base_HI.mp3 or base_hi.mp3
        snprintf(cand, sizeof(cand), "%s_%s.mp3", base, lang_upper);
        snprintf(full_path, sizeof(full_path), "%s%s", folder, cand);
        if (fs_size(full_path) > 0) {
            prv_add_audio_name(buf, buf_size, cand);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s_%s.mp3", base, lang_lower);
        snprintf(full_path, sizeof(full_path), "%s%s", folder, cand);
        if (fs_size(full_path) > 0) {
            prv_add_audio_name(buf, buf_size, cand);
            return true;
        }

        // 2. Middle: id_HI_X.mp3 or id_hi_X.mp3 (if starts with "id_")
        if (strncmp(base, "id_", 3) == 0) {
            snprintf(cand, sizeof(cand), "id_%s_%s.mp3", lang_upper, base + 3);
            snprintf(full_path, sizeof(full_path), "%s%s", folder, cand);
            if (fs_size(full_path) > 0) {
                prv_add_audio_name(buf, buf_size, cand);
                return true;
            }
            snprintf(cand, sizeof(cand), "id_%s_%s.mp3", lang_lower, base + 3);
            snprintf(full_path, sizeof(full_path), "%s%s", folder, cand);
            if (fs_size(full_path) > 0) {
                prv_add_audio_name(buf, buf_size, cand);
                return true;
            }
        }

        // 3. Prefix: HI_fname or hi_fname
        snprintf(cand, sizeof(cand), "%s_%s", lang_upper, fname);
        snprintf(full_path, sizeof(full_path), "%s%s", folder, cand);
        if (fs_size(full_path) > 0) {
            prv_add_audio_name(buf, buf_size, cand);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s_%s", lang_lower, fname);
        snprintf(full_path, sizeof(full_path), "%s%s", folder, cand);
        if (fs_size(full_path) > 0) {
            prv_add_audio_name(buf, buf_size, cand);
            return true;
        }
    }

    // Direct match
    snprintf(full_path, sizeof(full_path), "%s%s", folder, fname);
    if (fs_size(full_path) > 0) {
        prv_add_audio_name(buf, buf_size, fname);
        return true;
    }

    // Check inside subfolder hindi_id/ if language is Hindi
    if (prv_is_lang_hindi(lang)) {
        snprintf(full_path, sizeof(full_path), "%shindi_id/%s", folder, fname);
        if (fs_size(full_path) > 0) {
            char sub_fname[64];
            snprintf(sub_fname, sizeof(sub_fname), "hindi_id/%s", fname);
            prv_add_audio_name(buf, buf_size, sub_fname);
            return true;
        }
    }

    // Fallback: check root D:/
    snprintf(full_path, sizeof(full_path), "D:/%s", fname);
    if (fs_size(full_path) > 0) {
        prv_add_audio_name(buf, buf_size, fname);
        return true;
    }
    return false;
}

// add by vipin: add audio file checking language variations, 2-digit padding, and currency aliases in D:/FEMALE/
static int prv_add_lang_audio(char *buf, size_t buf_size, const char *filename, const char *lang) {
    const char *folder = AUDIO_FOLDER_PATH;
    if (lang && lang[0] != '\0') {
        soundbox_language_t l;
        if (soundbox_str_to_language(lang, &l)) {
            const char *f = soundbox_get_audio_folder(l);
            if (f != NULL) {
                folder = f;
            }
        }
    }

    // 1. Try direct / lang variants for primary filename
    if (prv_try_file_with_lang(buf, buf_size, folder, filename, lang)) {
        return 0;
    }

    // 2. Zero-padded / unpadded alias: e.g. "id_1.mp3" <-> "id_01.mp3"
    int single_num = -1;
    if (sscanf(filename, "id_%d.mp3", &single_num) == 1 && single_num >= 0 && single_num <= 9) {
        char cand_num[32];
        snprintf(cand_num, sizeof(cand_num), "id_%02d.mp3", single_num);
        if (prv_try_file_with_lang(buf, buf_size, folder, cand_num, lang)) {
            return 0;
        }
        snprintf(cand_num, sizeof(cand_num), "id_%d.mp3", single_num);
        if (prv_try_file_with_lang(buf, buf_size, folder, cand_num, lang)) {
            return 0;
        }
    }

    // 3. Currency / unit aliases (rupees <-> rupee, paisa <-> paise)
    if (strcmp(filename, "id_rupees.mp3") == 0) {
        if (prv_try_file_with_lang(buf, buf_size, folder, "id_rupee.mp3", lang)) return 0;
    } else if (strcmp(filename, "id_rupee.mp3") == 0) {
        if (prv_try_file_with_lang(buf, buf_size, folder, "id_rupees.mp3", lang)) return 0;
    } else if (strcmp(filename, "id_paisa.mp3") == 0) {
        if (prv_try_file_with_lang(buf, buf_size, folder, "id_paise.mp3", lang)) return 0;
    } else if (strcmp(filename, "id_paise.mp3") == 0) {
        if (prv_try_file_with_lang(buf, buf_size, folder, "id_paisa.mp3", lang)) return 0;
    }

    // 4. Optional prompts: if thank_you not present, omit gracefully instead of adding missing file
    if (strncmp(filename, "id_thank_you", 12) == 0) {
        FRAMEWORK_LOG("Optional audio %s not present, skipping", filename);
        return 0;
    }

    // Default fallback
    return prv_add_audio_name(buf, buf_size, filename);
}

// add by vipin: check if language is Hindi
static bool prv_is_lang_hindi(const char *lang) {
    if (!lang) return false;
    if (strcmp(lang, "1") == 0) return true;
    if (lang[0] == 'H' || lang[0] == 'h') {
        if (lang[1] == 'I' || lang[1] == 'i') {
            return (lang[2] == '\0' || 
                   ((lang[2] == 'N' || lang[2] == 'n') &&
                    (lang[3] == 'D' || lang[3] == 'd') &&
                    (lang[4] == 'I' || lang[4] == 'i') && lang[5] == '\0'));
        }
    }
    return false;
}

// add by vipin: check if Indian Lakh audio exists in D:/FEMALE/
static bool prv_has_lakh_audio(const char *lang) {
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%sid_lakh.mp3", AUDIO_FOLDER_PATH);
    if (fs_size(full_path) > 0) return true;
    snprintf(full_path, sizeof(full_path), "%sid_lakh_%s.mp3", AUDIO_FOLDER_PATH, lang);
    if (fs_size(full_path) > 0) return true;
    snprintf(full_path, sizeof(full_path), "%sid_lac.mp3", AUDIO_FOLDER_PATH);
    if (fs_size(full_path) > 0) return true;
    snprintf(full_path, sizeof(full_path), "%slakh.mp3", AUDIO_FOLDER_PATH);
    return (fs_size(full_path) > 0);
}

// =============================================================================
// HINDI (HI) PAYMENT AUDIO HANDLER
// Sequence: [Rupees] रुपये [Paise] पैसे प्राप्त हुए
// All numbers 1-99 are direct single files (id_01.mp3 to id_99.mp3)
// =============================================================================

static int prv_hindi_units_to_seq(int n, char *buf, size_t buf_size) {
    if (n <= 0) return 0;
    int hundreds = n / 100;
    int rem = n % 100;
    int ret = 0;

    if (hundreds > 0) {
        char tmp[32];
        if (hundreds < 10) {
            snprintf(tmp, sizeof(tmp), "id_%02d.mp3", hundreds);
        } else {
            snprintf(tmp, sizeof(tmp), "id_%d.mp3", hundreds);
        }
        ret = prv_add_lang_audio(buf, buf_size, tmp, "HI");
        if (ret) return ret;

        ret = prv_add_lang_audio(buf, buf_size, "id_hundred.mp3", "HI");
        if (ret) return ret;
    }

    if (rem > 0) {
        char direct_file[32];
        if (rem < 10) {
            snprintf(direct_file, sizeof(direct_file), "id_%02d.mp3", rem);
        } else {
            snprintf(direct_file, sizeof(direct_file), "id_%d.mp3", rem);
        }
        ret = prv_add_lang_audio(buf, buf_size, direct_file, "HI");
        if (ret) return ret;
    }
    return 0;
}

static int prv_hindi_number_to_seq(uint64_t n, char *buf, size_t buf_size) {
    int ret = 0;
    if (n <= 0) return 0;

    // Crores (1,00,00,000)
    uint64_t crores = n / 10000000;
    if (crores > 0) {
        ret = prv_hindi_units_to_seq((int)crores, buf, buf_size);
        if (ret) return ret;
        ret = prv_add_lang_audio(buf, buf_size, "id_crore.mp3", "HI");
        if (ret) return ret;
        n %= 10000000;
    }

    // Lakhs (1,00,000)
    uint64_t lakhs = n / 100000;
    if (lakhs > 0) {
        ret = prv_hindi_units_to_seq((int)lakhs, buf, buf_size);
        if (ret) return ret;
        ret = prv_add_lang_audio(buf, buf_size, "id_lakh.mp3", "HI");
        if (ret) return ret;
        n %= 100000;
    }

    // Thousands (1,000)
    uint64_t thousands = n / 1000;
    if (thousands > 0) {
        ret = prv_hindi_units_to_seq((int)thousands, buf, buf_size);
        if (ret) return ret;
        ret = prv_add_lang_audio(buf, buf_size, "id_thousand.mp3", "HI");
        if (ret) return ret;
        n %= 1000;
    }

    // Remaining hundreds & units (1-999)
    if (n > 0) {
        ret = prv_hindi_units_to_seq((int)n, buf, buf_size);
        if (ret) return ret;
    }

    return 0;
}

// add by vipin: dedicated Hindi payment audio generator
static int prv_generate_hindi_payment_audio(uint64_t rupees, uint32_t paise, char *audio_list, size_t list_size)
{
    int ret = 0;

    // 1. Rupees part (e.g. 24 -> "id_24.mp3" + "id_rupee.mp3")
    if (rupees > 0) {
        ret = prv_hindi_number_to_seq(rupees, audio_list, list_size);
        if (ret) return ret;

        ret = prv_add_lang_audio(audio_list, list_size, "id_rupee.mp3", "HI");
        if (ret) return ret;
    }

    // 2. Paise part (e.g. 34 -> "id_34.mp3" + "id_paise.mp3")
    if (paise > 0) {
        ret = prv_hindi_number_to_seq((uint64_t)paise, audio_list, list_size);
        if (ret) return ret;

        ret = prv_add_lang_audio(audio_list, list_size, "id_paise.mp3", "HI");
        if (ret) return ret;
    }

    // 3. Zero amount handling
    if (rupees == 0 && paise == 0) {
        ret = prv_add_lang_audio(audio_list, list_size, "id_0.mp3", "HI");
        if (ret == 0) {
            prv_add_lang_audio(audio_list, list_size, "id_rupee.mp3", "HI");
        }
    }

    // 4. "id_received.mp3" ("प्राप्त हुए") at the END for Hindi
    ret = prv_add_lang_audio(audio_list, list_size, "id_received.mp3", "HI");
    if (ret) return ret;

    return 0;
}

// =============================================================================
// ENGLISH (EN) PAYMENT AUDIO HANDLER
// Sequence: Received [Rupees] Rupees [Paise] Paise Thank you
// Numbers 20-99 are split into tens + units (e.g. 24 -> 20 + 4)
// =============================================================================

static int prv_english_units_to_seq(int n, char *buf, size_t buf_size) {
    if (n <= 0) return 0;
    int hundreds = n / 100;
    int rem = n % 100;
    int ret = 0;

    if (hundreds > 0) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "id_%d.mp3", hundreds);
        ret = prv_add_lang_audio(buf, buf_size, tmp, "EN");
        if (ret) return ret;

        ret = prv_add_lang_audio(buf, buf_size, "id_hundred.mp3", "EN");
        if (ret) return ret;
    }

    if (rem > 0) {
        if (rem < 20) {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "id_%d.mp3", rem);
            ret = prv_add_lang_audio(buf, buf_size, tmp, "EN");
            if (ret) return ret;
        } else {
            // Check if direct single-word file exists (e.g. id_24_EN.mp3 or id_24.mp3)
            char direct_file[32];
            snprintf(direct_file, sizeof(direct_file), "id_%d.mp3", rem);
            if (prv_try_file_with_lang(buf, buf_size, AUDIO_FOLDER_PATH, direct_file, "EN")) {
                // Direct file added
            } else {
                int tens = (rem / 10) * 10;
                int unit = rem % 10;
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "id_%d.mp3", tens);
                ret = prv_add_lang_audio(buf, buf_size, tmp, "EN");
                if (ret) return ret;
                if (unit > 0) {
                    snprintf(tmp, sizeof(tmp), "id_%d.mp3", unit);
                    ret = prv_add_lang_audio(buf, buf_size, tmp, "EN");
                    if (ret) return ret;
                }
            }
        }
    }
    return 0;
}

static int prv_english_number_to_seq(uint64_t n, char *buf, size_t buf_size) {
    int ret = 0;
    if (n <= 0) return 0;

    if (prv_has_lakh_audio("EN")) {
        // Indian Numbering for English (Crores, Lakhs, Thousands, Hundreds)
        uint64_t crores = n / 10000000;
        if (crores > 0) {
            ret = prv_english_units_to_seq((int)crores, buf, buf_size);
            if (ret) return ret;
            ret = prv_add_lang_audio(buf, buf_size, "id_crore.mp3", "EN");
            if (ret) return ret;
            n %= 10000000;
        }

        uint64_t lakhs = n / 100000;
        if (lakhs > 0) {
            ret = prv_english_units_to_seq((int)lakhs, buf, buf_size);
            if (ret) return ret;
            ret = prv_add_lang_audio(buf, buf_size, "id_lakh.mp3", "EN");
            if (ret) return ret;
            n %= 100000;
        }
    } else {
        // Western Numbering for English (Millions, Thousands, Hundreds)
        uint64_t millions = n / 1000000;
        if (millions > 0) {
            ret = prv_english_units_to_seq((int)millions, buf, buf_size);
            if (ret) return ret;
            ret = prv_add_lang_audio(buf, buf_size, "id_million.mp3", "EN");
            if (ret) return ret;
            n %= 1000000;
        }
    }

    uint64_t thousands = n / 1000;
    if (thousands > 0) {
        ret = prv_english_units_to_seq((int)thousands, buf, buf_size);
        if (ret) return ret;
        ret = prv_add_lang_audio(buf, buf_size, "id_thousand.mp3", "EN");
        if (ret) return ret;
        n %= 1000;
    }

    if (n > 0) {
        ret = prv_english_units_to_seq((int)n, buf, buf_size);
        if (ret) return ret;
    }

    return 0;
}

// add by vipin: dedicated English payment audio generator
static int prv_generate_english_payment_audio(uint64_t rupees, uint32_t paise, char *audio_list, size_t list_size)
{
    int ret = 0;

    // 1. "id_received.mp3" at START for English
    ret = prv_add_lang_audio(audio_list, list_size, "id_received.mp3", "EN");
    if (ret) return ret;

    // 2. Rupees portion
    if (rupees > 0) {
        ret = prv_english_number_to_seq(rupees, audio_list, list_size);
        if (ret) return ret;

        if (rupees == 1) {
            ret = prv_add_lang_audio(audio_list, list_size, "id_rupee.mp3", "EN");
            if (ret) return ret;
        } else {
            ret = prv_add_lang_audio(audio_list, list_size, "id_rupees.mp3", "EN");
            if (ret) return ret;
        }
    }

    // 3. Paise portion
    if (paise > 0) {
        ret = prv_english_units_to_seq((int)paise, audio_list, list_size);
        if (ret) return ret;

        if (paise == 1) {
            ret = prv_add_lang_audio(audio_list, list_size, "id_paisa.mp3", "EN");
            if (ret) return ret;
        } else {
            ret = prv_add_lang_audio(audio_list, list_size, "id_paise.mp3", "EN");
            if (ret) return ret;
        }
    }

    // 4. Zero amount handling
    if (rupees == 0 && paise == 0) {
        ret = prv_add_lang_audio(audio_list, list_size, "id_0.mp3", "EN");
        if (ret == 0) {
            prv_add_lang_audio(audio_list, list_size, "id_rupees.mp3", "EN");
        }
    }

    // 5. "id_thank_you.mp3" at END for English
    ret = prv_add_lang_audio(audio_list, list_size, "id_thank_you.mp3", "EN");
    if (ret) return ret;

    return 0;
}

// add by vipin: main dispatcher routing to dedicated English or Hindi handler
static int prv_generate_amount_audio_list(const payment_broadcast_params_t *params, char *audio_list, size_t list_size)
{
    audio_list[0] = '\0';

    const char *lang = (params->language[0] != '\0') ? params->language : customer_get_default_language();
    uint64_t rupees = params->rupees;
    uint32_t paise = params->paise;

    // Legacy fallback: if rupees and paise both 0 but amount > 0
    if (rupees == 0 && paise == 0 && params->amount > 0) {
        rupees = params->amount;
    }

    // Call respective dedicated handler
    if (prv_is_lang_hindi(lang)) {
        return prv_generate_hindi_payment_audio(rupees, paise, audio_list, list_size);
    } else {
        return prv_generate_english_payment_audio(rupees, paise, audio_list, list_size);
    }
}

static payment_broadcast_params_t s_last_payment_params = {0};
static bool s_has_last_payment = false;
static uint32_t s_total_txn_count = 0; // add by vipin: total transaction counter
static uint32_t s_last_txn_count = 0;  // add by vipin: transaction count of last payment

#ifndef CUSTOMER_LCD_SHOW_TXN_COUNT
#define CUSTOMER_LCD_SHOW_TXN_COUNT 1 // add by vipin: 1 = Show Total Txn Count on LCD side display, 0 = Hide side count
#endif

#ifndef CUSTOMER_LCD_IDLE_TIMEOUT_MS
#define CUSTOMER_LCD_IDLE_TIMEOUT_MS 10000 // add by vipin: 10 seconds amount display before idle screen
#endif

#if defined(CUSTOMER_ENABLE_LCD) && (CUSTOMER_ENABLE_LCD == 1)
static osiTimer_t *s_lcd_idle_timer = NULL;
static osiWork_t *s_lcd_idle_work = NULL;

static osiTimer_t *s_lcd_rotate_timer = NULL;
static osiWork_t *s_lcd_rotate_work = NULL;

typedef enum {
    LCD_ROTATE_HITACHI = 0,
    LCD_ROTATE_TIME    = 1,
    LCD_ROTATE_DATE    = 2,
} lcd_rotate_stage_t;

static lcd_rotate_stage_t s_lcd_rotate_stage = LCD_ROTATE_HITACHI;
static bool s_is_payment_active = false;

static void prv_lcd_rotate_timer_callback(void *param);

static void prv_lcd_rotate_work_callback(void *param)
{
    (void)param;
    if (s_is_payment_active) {
        return; // Payment display takes precedence, do not rotate
    }

    switch (s_lcd_rotate_stage) {
    case LCD_ROTATE_HITACHI:
        FRAMEWORK_LOG("[LCD] Rotating Screen (1/3): HITACHI");
        appLcdShowTextHitachi();
        s_lcd_rotate_stage = LCD_ROTATE_TIME;
        break;
    case LCD_ROTATE_TIME:
        FRAMEWORK_LOG("[LCD] Rotating Screen (2/3): TIME");
        appLcdShowTime();
        s_lcd_rotate_stage = LCD_ROTATE_DATE;
        break;
    case LCD_ROTATE_DATE:
        FRAMEWORK_LOG("[LCD] Rotating Screen (3/3): DATE");
        appLcdShowDate();
        s_lcd_rotate_stage = LCD_ROTATE_HITACHI;
        break;
    default:
        s_lcd_rotate_stage = LCD_ROTATE_HITACHI;
        appLcdShowTextHitachi();
        break;
    }

    // Schedule next 5-second rotation
    if (!s_is_payment_active && s_lcd_rotate_timer != NULL) {
        osiTimerStart(s_lcd_rotate_timer, 5000);
    }
}

static void prv_lcd_rotate_timer_callback(void *param)
{
    (void)param;
    if (s_is_payment_active) {
        return;
    }
    if (s_lcd_rotate_timer != NULL) {
        osiTimerStop(s_lcd_rotate_timer);
    }
    if (s_lcd_rotate_work == NULL) {
        s_lcd_rotate_work = osiWorkCreate(prv_lcd_rotate_work_callback, NULL, NULL);
    }
    if (s_lcd_rotate_work != NULL) {
        osiWorkEnqueue(s_lcd_rotate_work, osiSysWorkQueueLowPriority());
    }
}

static void prv_lcd_idle_work_callback(void *param)
{
    (void)param;
    FRAMEWORK_LOG("LCD 10s payment timeout reached, resuming rotating idle screens");
    s_is_payment_active = false;
    s_lcd_rotate_stage = LCD_ROTATE_TIME; // We display HITACHI now, next 5s rotation is TIME
    appLcdShowTextHitachi();

    // Start 5s rotation from HITACHI -> TIME -> DATE
    if (s_lcd_rotate_timer == NULL) {
        s_lcd_rotate_timer = osiTimerCreate(prv_lcd_rotate_timer_callback, NULL);
    }
    if (s_lcd_rotate_timer != NULL) {
        osiTimerStop(s_lcd_rotate_timer);
        osiTimerStart(s_lcd_rotate_timer, 5000);
    }
}

static void prv_lcd_idle_timer_callback(void *param)
{
    (void)param;
    if (s_lcd_idle_timer != NULL) {
        osiTimerStop(s_lcd_idle_timer);
    }
    if (s_lcd_idle_work == NULL) {
        s_lcd_idle_work = osiWorkCreate(prv_lcd_idle_work_callback, NULL, NULL);
    }
    if (s_lcd_idle_work != NULL) {
        osiWorkEnqueue(s_lcd_idle_work, osiSysWorkQueueLowPriority());
    }
}

void customer_lcd_start_rotating_idle(void)
{
    FRAMEWORK_LOG("Starting 5s rotating idle screen (HITACHI -> TIME -> DATE)");
    s_is_payment_active = false;
    s_lcd_rotate_stage = LCD_ROTATE_TIME; // After initial HITACHI, next is TIME
    appLcdShowTextHitachi();

    if (s_lcd_rotate_timer == NULL) {
        s_lcd_rotate_timer = osiTimerCreate(prv_lcd_rotate_timer_callback, NULL);
    }
    if (s_lcd_rotate_timer != NULL) {
        osiTimerStop(s_lcd_rotate_timer);
        osiTimerStart(s_lcd_rotate_timer, 5000);
    }
}

void customer_lcd_stop_rotating_idle(void)
{
    if (s_lcd_rotate_timer != NULL) {
        osiTimerStop(s_lcd_rotate_timer);
    }
}
#else
void customer_lcd_start_rotating_idle(void) {}
void customer_lcd_stop_rotating_idle(void) {}
#endif

static char s_pending_txn_id[64] = {0};
static char s_pending_amount_str[16] = {0};
static char s_pending_language[16] = {0};

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

        /* Trigger active campaign MP3 playback after transaction audio finishes */
        customer_campaign_on_transaction_completed();
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

    const char *lang_str = (params->language[0] != '\0') ? params->language : customer_get_default_language();
    const char *active_folder = AUDIO_FOLDER_PATH;
    soundbox_language_t l;
    if (soundbox_str_to_language(lang_str, &l)) {
        const char *f = soundbox_get_audio_folder(l);
        if (f != NULL) {
            active_folder = f;
        }
    }
    audio_resource_set_language_prefix(active_folder);

    FRAMEWORK_LOG("Payment broadcast: rupees=%llu, paise=%lu, total=%.2f, lang=%s, folder=%s, is_replay=%d", 
                  (unsigned long long)params->rupees, (unsigned long)params->paise, params->total_amount,
                  lang_str, active_folder, is_replay);

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
            strncpy(s_pending_language, customer_get_default_language(), sizeof(s_pending_language) - 1);
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
    // add by vipin: Stop rotating idle screen immediately and prioritize payment display
    s_is_payment_active = true;
    if (s_lcd_rotate_timer != NULL) {
        osiTimerStop(s_lcd_rotate_timer);
    }

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

    // add by vipin: Show amount for 10 seconds then resume rotating idle screen
    if (s_lcd_idle_timer == NULL) {
        s_lcd_idle_timer = osiTimerCreate(prv_lcd_idle_timer_callback, NULL);
    }
    if (s_lcd_idle_timer != NULL) {
        osiTimerStop(s_lcd_idle_timer);
        osiTimerStart(s_lcd_idle_timer, CUSTOMER_LCD_IDLE_TIMEOUT_MS);
    }
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
