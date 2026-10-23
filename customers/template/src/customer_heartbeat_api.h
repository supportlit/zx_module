#ifndef CUSTOMER_HEARTBEAT_API_H
#define CUSTOMER_HEARTBEAT_API_H
#include "cJSON.h"
int prv_send_json(cJSON *json_body);
#endif