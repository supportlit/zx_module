/**
 * @file customer_campaign.h
 * @brief TMS Campaign MP3 Download and Post-Transaction Playback Manager
 */

#ifndef _CUSTOMER_CAMPAIGN_H_
#define _CUSTOMER_CAMPAIGN_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMPAIGN_ACTIVE_FILE_PATH "D:/campaign.mp3"
#define CAMPAIGN_TEMP_FILE_PATH   "D:/campaign_temp.mp3"

/**
 * @brief Initialize campaign subsystem
 */
void customer_campaign_init(void);

/**
 * @brief Process Field 4 from the transaction MQTT message
 *
 * Extracts, trims, normalizes, classifies (filename vs URL-like),
 * validates security/allowlist, constructs standard TMS download URL,
 * and starts asynchronous download and activation if needed.
 *
 * @param field4 Raw 4th token string from MQTT payload
 * @return 0 on success/queued/already-active, negative on validation/rejection error
 */
int customer_campaign_process_field(const char *field4);

/**
 * @brief Check if a valid active campaign audio file is ready for playback
 * @return true if valid active campaign file exists on disk
 */
bool customer_campaign_has_active(void);

/**
 * @brief Play the active campaign audio after transaction completion
 * @return 0 on success, negative on error
 */
int customer_campaign_on_transaction_completed(void);

/**
 * @brief Get the currently active campaign name
 * @return Active campaign name or ""
 */
const char *customer_campaign_get_active_name(void);

#ifdef __cplusplus
}
#endif

#endif /* _CUSTOMER_CAMPAIGN_H_ */
