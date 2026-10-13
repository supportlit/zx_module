/**
 * @file framework_lcd7567.h
 * @brief Audio Resource Manager for ZxInfotek Soundbox
 * @version 1.0
 * @date 2025-11-20
 * 
 * Audio resource management system supporting multi-language audio playback
 * for system prompts, volume indicators, battery status, network status,
 * payment confirmations, and bank notifications.
 */

#ifndef __FRAMEWORK_LCD7567_H__
#define __FRAMEWORK_LCD7567_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//icon level
#define FRAMEWORK_ICON_LEVEL0 (1 << 0)
#define FRAMEWORK_ICON_LEVEL1 (FRAMEWORK_ICON_LEVEL0 | (1 << 1))
#define FRAMEWORK_ICON_LEVEL2 (FRAMEWORK_ICON_LEVEL1 | (1 << 2))
#define FRAMEWORK_ICON_LEVEL3 (FRAMEWORK_ICON_LEVEL2 | (1 << 3))
#define FRAMEWORK_ICON_LEVEL4 (FRAMEWORK_ICON_LEVEL3 | (1 << 4))
#define FRAMEWORK_ICON_LEVEL5 (FRAMEWORK_ICON_LEVEL4 | (1 << 5))


int framework_lcd7567_Init(void);
void framework_lcd7567_ALLflush(void);
void framework_lcd7567_full_All_display(bool value);
void framework_lcd7567_cellnet_ctrl(bool value);
void framework_lcd7567_battery_ctrl(uint8_t value, uint8_t level);
void framework_lcd7567_wifi_ctrl(bool value);
void framework_lcd7567_sign_ctrl(uint8_t value, uint8_t level);
void framework_lcd7567_ntd_display(char *str);
void framework_lcd7567_extraNum_display(char *str);
void framework_lcd7567_time_display(char *str);

int framework_lcd7567_get_backlight_time(void);
int framework_lcd7567_set_backlight(uint8_t en);
int framework_lcd7567_get_backlight_status(void);
int framework_lcd7567_set_backlight_time(int second);


void framework_lcd7567_startup_display(uint8_t index);
#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_LCD7567_H__ */