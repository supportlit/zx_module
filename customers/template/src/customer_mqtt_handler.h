/**
 * @file customer_mqtt_handler.h
 * @brief template 云喇叭 MQTT 处理器头文件
 */

#ifndef __CUSTOMER_MQTT_HANDLER_H__
#define __CUSTOMER_MQTT_HANDLER_H__

#include "framework_msg.h"

int customer_mqtt_init(void);
int customer_mqtt_network_ready(void);

void template_mqtt_handle_http_msg(framework_msg_t *msg);
void template_mqtt_handle_unzip_msg(framework_msg_t *msg);

void customer_language_init(void);
const char *customer_get_default_language(void);
void customer_set_default_language(const char *lang);

#endif /* __CUSTOMER_MQTT_HANDLER_H__ */
