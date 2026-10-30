/**
 * @file customer_tms_update.h
 * @brief TMS (Terminal Management System) OTA & Audio Update Handler
 */

#ifndef __CUSTOMER_TMS_UPDATE_H__
#define __CUSTOMER_TMS_UPDATE_H__

#include <stdint.h>
#include <stdbool.h>
#include "framework_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Trigger TMS version check and update process
 * @details Can be called from MQTT (/tmsUpdate) or timer/boot
 * @return 0 on success, negative error code otherwise
 */
int customer_tms_update_start(void);

/**
 * @brief Handle unzip messages for audio updates
 * @param msg Pointer to framework message
 */
void customer_tms_update_handle_unzip_msg(framework_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* __CUSTOMER_TMS_UPDATE_H__ */
