#ifndef CUSTOMER_CHANGE_LANGUAGE_ACK_H
#define CUSTOMER_CHANGE_LANGUAGE_ACK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Send Language Change Acknowledgement to TMS
 * 
 * Target URL: https://1.7.140.58/pos/TMS/LanguageFile/ChangeLanguageacknowledgement
 * 
 * Sample Payload:
 * {
 *     "request_id": 202540563751,
 *     "deviceId": "00001504QR70W031163",
 *     "status": " Success ",
 *     "reason": "",
 *     "active_language": "1"
 * }
 * 
 * @param request_id Request ID (e.g. 202540563751.0; if <=0 uses sample default 202540563751)
 * @param device_id Device ID (e.g. "00001504QR70W031163"; if NULL/empty uses sample default)
 * @param status Status string (e.g. " Success " or "Failed"; if NULL/empty uses sample default)
 * @param reason Reason string ("" on success, error detail on failure)
 * @param active_language Active language (e.g. "1"; if NULL/empty uses sample default "1")
 * @return 0 on success queuing, negative on error
 */
int customer_send_change_language_ack(double request_id, const char *device_id,
                                     const char *status, const char *reason,
                                     const char *active_language);

#endif /* CUSTOMER_CHANGE_LANGUAGE_ACK_H */
