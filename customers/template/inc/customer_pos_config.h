/**
 * @file customer_pos_config.h
 * @brief POS Configuration Management Module (TMS getConfigDetailByTid)
 */

#ifndef __CUSTOMER_POS_CONFIG_H__
#define __CUSTOMER_POS_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POS_CONFIG_FILE_PATH "D:/template_pos_config.json"

typedef struct {
    bool valid;
    uint32_t pos_config_id;
    char mid[32];
    char tid[32];
    char serial_number[36];
    char merchant_name[64];
    char merchant_location[64];
    char merchant_city[32];
    char merchant_state[32];
    char merchant_phone[20];
    char currency_code[8];
    char country_code[8];
    char mcc[16];
    char vpa[64];
    char upi_device_id[32];
    char store_number[16];
    char processor[32];
    char developer_id[32];
    char merchant_type[32];
    char acquirer_bin_id[32];
    char acquirer_bin_tsys[32];
    char agent_bank_tsys[32];
    char agent_chain_tsys[32];
    char gen_key_tsys[64];
    char datawire_url[64];
    char qsr_limit[16];
    char user_id[16];
    char user_name[32];
} customer_pos_config_t;

/**
 * @brief Initialize POS config from local storage at boot
 * @return 0 if file loaded and valid, negative otherwise (needs fetch)
 */
int customer_pos_config_init(void);

/**
 * @brief Check if POS configuration is loaded in memory
 * @return true if valid configuration is loaded
 */
bool customer_pos_config_is_ready(void);

/**
 * @brief Asynchronously fetch POS config from TMS via HTTP GET
 *        URL: https://103.157.22.211:8075/sxtms/online/tms/getConfigDetailByTid/<SN>
 * @return 0 if request queued, negative on error
 */
int customer_pos_config_fetch_async(void);

/**
 * @brief Get read-only pointer to in-memory POS config struct
 * @return Pointer to struct, or NULL if not ready
 */
const customer_pos_config_t* customer_pos_config_get(void);

/**
 * @brief Helper getters for commonly used fields (returns empty string "" if not set)
 */
const char* customer_pos_config_get_mid(void);
const char* customer_pos_config_get_tid(void);
const char* customer_pos_config_get_merchant_name(void);
const char* customer_pos_config_get_vpa(void);
const char* customer_pos_config_get_sn(void);

#ifdef __cplusplus
}
#endif

#endif /* __CUSTOMER_POS_CONFIG_H__ */

