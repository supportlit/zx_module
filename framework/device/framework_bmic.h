#ifndef FRAMEWORK_BMIC_H
#define FRAMEWORK_BMIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BMIC/Charger hardware definitions */
#define BMIC_I2C_NUMBER (0)
#define I2C_SCL         (GPIO_49)
#define I2C_SDA         (GPIO_50)

#define WAKEUP_PORT             (GPIO_54)
#define WAKEUP_PORT_OUTPUT      (54)
#define SD77428_COULOMB_COUNTER_ADDR    (0xAA>>1)
#define BMIC_SLAVE_ADDR                 (SD77428_COULOMB_COUNTER_ADDR)
#define CMD_StateOfHealtPercent         (0x20)
#define CMD_Voltage                     (0x04)
#define CMD_SLEEP_TIME                  (0x74)
#define CMD_INTERNAL_TEMPERATURE        (0x1e)

/* Charge IC addresses (enable the one you use) */
/* #define SD7601D_CHARGER_ADDR (0xD6>>1) */
/* #define SCV89602D_CHARGER_ADDR (0x6B)  */
#define SGM41512S_CHARGER_ADDR (0x1a)

int framework_bmic_init(void);
int framework_bmic_sleep(void);
int framework_bmic_wakeup(void);
int framework_bmic_shipping_mode_enable(void);
int framework_bmic_shipping_mode_disable(void);
int framework_bmic_read_voltage(uint16_t *millivolt);   /* returns 0 on success */
int framework_bmic_read_soh(uint8_t *percent);          /* returns 0 on success */
int framework_bmic_read_soc(uint8_t *percent);          /* returns 0 on success */

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_PMIC_H */
