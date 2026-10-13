/**
 * @file factory_at_cmds.h
 * @brief Factory mode AT command handlers
 */

#ifndef FACTORY_AT_CMDS_H
#define FACTORY_AT_CMDS_H

#include "factory_at.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*at_handler_f)(void *param);

typedef struct {
    at_handler_f led_handler;
    at_handler_f key_handler;
} factory_at_cmd_handlers_t;

/* Command initialization */
void factory_at_cmds_init(void);

/* System commands */
at_ret_t at_cmd_firmware(void);         /* AT+FIRMWARE - Report firmware version */
at_ret_t at_cmd_restart(void);          /* AT+RESTART - Restart system */

/* LED commands */
at_ret_t at_cmd_ctl_red(void);          /* AT+CTL_R - Control red LED */
at_ret_t at_cmd_ctl_green(void);        /* AT+CTL_G - Control green LED */
at_ret_t at_cmd_ctl_blue(void);         /* AT+CTL_B - Control blue LED */
at_ret_t at_cmd_ctl_off(void);          /* AT+CTL_OFF - Turn off all LEDs */

/* Key test command */
at_ret_t at_cmd_keytest(void);          /* AT+KEYTEST - Test all keys */

/* Audio commands */
at_ret_t at_cmd_spk_play(uint8_t id, int8_t *param);    /* AT+SPK=<file> - Play audio */
at_ret_t at_cmd_spk_stop(void);         /* AT+SPK_STOP - Stop audio */

/* File system commands */
at_ret_t at_cmd_fs_download(uint8_t id, int8_t *param); /* AT+FSDWNFILE=<name>,<size>,<offset> */
at_ret_t at_cmd_fs_read(uint8_t id, int8_t *param);     /* AT+FSRDFILE=<name> */
at_ret_t at_cmd_fs_delete(void);        /* AT+DEL - Delete test files */

/* Device SN commands */
at_ret_t at_cmd_set_sn(uint8_t id, int8_t *param);      /* AT+SETSN=<sn> */
at_ret_t at_cmd_get_sn(void);           /* AT+GETSN - Get device SN */

/* WiFi test commands */
at_ret_t at_cmd_wifi_test(uint8_t id, int8_t *param);   /* AT+WIFITEST=<ssid>,<pwd> */
at_ret_t at_cmd_wifi_csq(void);         /* AT+WIFICSQGET - Get WiFi signal quality */
at_ret_t at_cmd_wifi_version(void);     /* AT+WIFIVERSION - Get WiFi version */

/* Battery test command */
at_ret_t at_cmd_battery(void);          /* AT+BATTERY - Get battery voltage */

int factory_at_response_user(const int8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_AT_CMDS_H */
