/**
 * @file customer_battery_config.h
 * @brief template 电池配置头文件
 */

#ifndef __CUSTOMER_BATTERY_CONFIG_H__
#define __CUSTOMER_BATTERY_CONFIG_H__

#include <stdint.h>

#define CUSTOMER_BAT_DISCHARGE_VOLTAGE_STEP 11
#define CUSTOMER_BAT_CHARGE_VOLTAGE_STEP 11

extern const uint16_t customer_dischg_capacity_table[CUSTOMER_BAT_DISCHARGE_VOLTAGE_STEP][2];
extern const uint16_t customer_chg_capacity_table[CUSTOMER_BAT_CHARGE_VOLTAGE_STEP][2];

int customer_battery_config_register(void);

#endif /* __CUSTOMER_BATTERY_CONFIG_H__ */
