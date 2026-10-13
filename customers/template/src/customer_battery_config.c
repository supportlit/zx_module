/**
 * @file customer_battery_config.c
 * @brief template 电池电压-容量映射表
 */

#include "customer_battery_config.h"
#include "framework_battery.h"

const uint16_t customer_dischg_capacity_table[CUSTOMER_BAT_DISCHARGE_VOLTAGE_STEP][2] = {
    {4120, 100},
    {4022, 90},
    {3970, 80},
    {3894, 70},
    {3815, 60},
    {3769, 50},
    {3719, 40},
    {3648, 30},
    {3600, 20},
    {3574, 10},
    {3501, 0},
};

const uint16_t customer_chg_capacity_table[CUSTOMER_BAT_CHARGE_VOLTAGE_STEP][2] = {
    {4185, 100},
    {4145, 90},
    {4101, 80},
    {4057, 70},
    {4008, 60},
    {3955, 50},
    {3884, 40},
    {3811, 30},
    {3750, 20},
    {3676, 10},
    {3501, 0},
};

int customer_battery_config_register(void)
{
    static const battery_config_t config = {
        .discharge_table = customer_dischg_capacity_table,
        .discharge_table_size = CUSTOMER_BAT_DISCHARGE_VOLTAGE_STEP,
        .charge_table = customer_chg_capacity_table,
        .charge_table_size = CUSTOMER_BAT_CHARGE_VOLTAGE_STEP
    };
    return framework_battery_register_config(&config);
}
