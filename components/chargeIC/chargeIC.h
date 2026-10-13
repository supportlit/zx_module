#ifndef _CHARGEIC_H_
#define _CHARGEIC_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "typedef.h"
#include "mfpr_api.h"
#include "mfpr.h"
#include "gpio_api.h"

#define BMIC_I2C_NUMBER (0)
#define I2C_SCL         (GPIO_49)
#define I2C_SDA         (GPIO_50)

#define CHG_CONFIG_PORT      (CLK_REQ)
#define CHG_CONFIG_PORT_OUTPUT      (84)
#define CHG_CONFIG_PORT_FUN      (MFP_AF1)

//Coulombmeter
//SD77428
#define WAKEUP_PORT             (GPIO_54)
#define WAKEUP_PORT_OUTPUT      (54)
#define SD77428_COULOMB_COUNTER_ADDR    (0xAA>>1)
#define BMIC_SLAVE_ADDR                 (SD77428_COULOMB_COUNTER_ADDR)
#define CMD_StateOfHealtPercent         (0x20)
#define CMD_StateOfCharge               (0x1c)
#define CMD_SLEEP_TIME                  (0x74)
void pp_module_init(void);
int pp_bmic_write_word(uint8_t cmd, uint16_t data);
int pp_bmic_read_word(uint8_t cmd, uint16_t *data);
int pp_bmic_write_byte(uint8_t cmd, uint8_t data);
int pp_bmic_shutdown(void);
int pp_bmic_wakeup(void);
uint8_t pp_bmic_read_SOH_percent(void);
uint8_t pp_bmic_read_SOC_percent(void);
int pp_bmic_chargeDis(uint8_t value);

//charge ic
//SD7601D / SCV89602D / SGM41512S
// #define SD7601D_CHARGER_ADDR (0xD6>>1)
// #define SCV89602D_CHARGER_ADDR (0x6B)
#define SGM41512S_CHARGER_ADDR (0x1a)
void ChargeIC_Shipping_Mode(void);
void ChargeIC_Exit_Shipping_Mode(void);
uint8_t ChargeIC_ChargeStatus(void);

#endif
