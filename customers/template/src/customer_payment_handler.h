/**
 * @file customer_payment_handler.h
 * @brief template 支付播报处理器头文件
 */

#ifndef __CUSTOMER_PAYMENT_HANDLER_H__
#define __CUSTOMER_PAYMENT_HANDLER_H__


#include <stdint.h>

#define PAYMENT_AUDIO_FOLDER ""
typedef struct {
    uint64_t amount;        // Legacy field / integer rupees
    uint64_t rupees;        // add by vipin: integer part of amount (Rupees)
    uint32_t paise;         // add by vipin: decimal part of amount (Paise, 0-99)
    double total_amount;    // add by vipin: total amount in float/double
    uint32_t bank_bin;
    char transaction_id[64]; // Transaction ID for completion ACK
    char amount_str[16];     // add by vipin: Amount string for ACK (e.g. "16.32")
    char language[16];       // add by vipin: Language for ACK & audio resolution (e.g. "HI", "EN")
} payment_broadcast_params_t;

int customer_payment_broadcast(const payment_broadcast_params_t *params);
int customer_payment_replay_last(void);
uint32_t customer_payment_get_total_count(void); // add by vipin: get total successful transactions count
void customer_payment_reset_total_count(void);   // add by vipin: reset transaction counter (e.g. daily/boot)

#endif /* __CUSTOMER_PAYMENT_HANDLER_H__ */
