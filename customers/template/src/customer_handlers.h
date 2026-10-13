/**
 * @file customer_handlers.h
 * @brief template 客户主处理器头文件
 */

#ifndef __CUSTOMER_HANDLERS_H__
#define __CUSTOMER_HANDLERS_H__

#include "framework_msg.h"

int customer_msg_handler(framework_msg_t *msg);
void customer_init(void);

#endif /* __CUSTOMER_HANDLERS_H__ */
