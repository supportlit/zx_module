#ifndef CUSTOMER_LANGUAGE_ACK_H
#define CUSTOMER_LANGUAGE_ACK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Send Language File Download Acknowledgement to TMS
 * 
 * Target URL: https://1.7.140.58/pos/TMS/LanguageFile/DownloadLanguageacknowledgement
 * 
 * @param request_id Request ID (e.g. 202540563751.0; if <=0 uses sample default)
 * @param uuid UUID string (e.g. "202503240932385882"; if NULL/empty uses sample default)
 * @param device_id Device ID (e.g. "00001504QR70W031163"; if NULL/empty uses sample default)
 * @param status Status string ("Success" or "Failed")
 * @param reason Reason string ("" on success, error detail on failure)
 * @return 0 on success queuing, negative on error
 */
int customer_send_language_ack(double request_id, const char *uuid, const char *device_id,
                               const char *status, const char *reason);

#endif /* CUSTOMER_LANGUAGE_ACK_H */
