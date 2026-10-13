/**
 * @file framework_lcd7567.c
 * @brief Audio Resource Manager Implementation for ZxInfotek Soundbox
 * @version 1.0
 * @date 2025-11-20
 */

#include "framework_lcd7567.h"
#include "framework_log.h"
#include "audio_resource_map.h"  /* Customer-specific resource map */
#include "framework_config.h"      /* For GPIO definitions */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#ifndef __cplusplus
#ifndef bool
typedef unsigned char bool;
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#endif
#include "osi_api.h"               /* RTOS API */
#include "typedef.h"               /* Basic types */
#include "gpio_api.h"              /* GPIO API */
#include "drv_def.h"
#include "drv_iomux.h"
#include "lcd/drv_lcd_panel.h"

#define TAG "LCD7567"

// LCD SOFT IIC CONTROL
#define SDA(n) GpioSetLevel(LCD_SDA_PIN, (n))
#define SCL(n) GpioSetLevel(LCD_SCL_PIN, (n))
#define RES(n) GpioSetLevel(LCD_RST_PIN, (n))

static void nop(void)
{
    volatile  long a = 0, b = 0;
    volatile  int q, w;
    for (w = 0; w < 0x1F; w++)
    {
        for (q = 0; q < 0x8; q++)
        {
            a++;
            b++;
            a += b;
        }
    }
}

static void Delay(unsigned int dly)
{
    osiThreadSleepMs(5);
}

static void prvLcdGpioInit(void)
{
    unsigned int mfp_lcd[] = {
        MFP_REG(LCD_RST_PINMUX) | KEY_LCD_FUN | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE, /*RST gpio*/
        MFP_REG(LCD_SCL_PINMUX) | KEY_LCD_FUN | MFP_DRIVE_SLOW | MFP_PULL_NONE | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
        MFP_REG(LCD_SDA_PINMUX) | KEY_LCD_FUN | MFP_DRIVE_SLOW | MFP_PULL_NONE | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
        MFP_EOC};
    mfp_config(mfp_lcd);

    GPIOConfiguration cfg = {
        .pinDir = GPIO_OUT_PIN,
        .initLv = 1,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_NO_EDGE,
        .isr = NULL,
        .wu = NULL,
    };

    GpioInitConfiguration(LCD_RST_PIN, cfg);
    GpioInitConfiguration(LCD_SCL_PIN, cfg);
    GpioInitConfiguration(LCD_SDA_PIN, cfg);

    OSI_LOGI(TAG, "LCD GPIO INIT OK,RST[%D],SCL[%D],SDA[%D]",LCD_RST_PIN,LCD_SCL_PIN,LCD_SDA_PIN);
}

/*Software i2c*/
static void prv_I2c_Start(void)
{
    SDA(1);
    nop();
    SCL(1);
    nop();
    SDA(1);
    nop();
    SDA(0);
    nop();
}
static void prv_I2c_Stop(void)
{
    SCL(1);
    nop();
    SDA(0);
    nop();
    SDA(1);
    nop();
    SCL(0);
    nop();
    SDA(0);
    nop();
}

static void prv_I2c_WaitAck(void)
{
    uint8_t count = 0;
    nop();
    GPIOConfiguration cfg = {
        .pinDir = GPIO_IN_PIN,
        .initLv = 1,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_NO_EDGE,
        .isr = NULL,
        .wu = NULL,
    };
    while (GpioGetLevel(LCD_SDA_PIN))
    {
        GpioInitConfiguration(LCD_SDA_PIN, cfg);
        count++;
        nop();
        if (count > 100)
        {
            OSI_LOGE(TAG, "ACK TIME OUT ");
            break;
        }
    }
    SCL(1);
    nop();
    SCL(0);
    nop();
    GpioSetDirection(LCD_SDA_PIN, 1);
    SDA(0);
    nop();
}

static void prv_I2c_SCLByte(char SDA)
{
    for (int i = 0; i < 8; i++)
    {
        SCL(0);
        nop();
        if((SDA&0x80)==0x80)
            SDA(1);
        else
            SDA(0);
        nop();
        SCL(1);
        nop();
        SDA=SDA<<1;
    }
    SCL(0);
    GPIOConfiguration cfg = {
        .pinDir = GPIO_IN_PIN,
        .initLv = 1,
        .pinPull = GPIO_PULLUP_ENABLE,
        .pinEd = GPIO_NO_EDGE,
        .isr = NULL,
        .wu = NULL,
    };
    GpioInitConfiguration(LCD_SDA_PIN, cfg);
}


/***************写命令函数**************/
static void prv_I2c_write_com(unsigned char cmd)
{
    prv_I2c_Start();
    prv_I2c_SCLByte(0x7e);
    prv_I2c_WaitAck();

    prv_I2c_SCLByte(0x00);
    prv_I2c_WaitAck();

    prv_I2c_SCLByte(cmd);
    prv_I2c_WaitAck();

    prv_I2c_Stop();

    nop();
}

void prv_I2c_write_data(unsigned char para)
{
	prv_I2c_Start();
    prv_I2c_SCLByte(0x7e);
    prv_I2c_WaitAck();
    prv_I2c_SCLByte(0x40);
    prv_I2c_WaitAck();
    prv_I2c_SCLByte(para);
    prv_I2c_WaitAck();
    prv_I2c_Stop();
  //	Delay(1);
}

int prv_lcd7567_init(void)
{
    // osiSetPMEnableSleep(false);
    // drvIsinkPwmEnable(true, 3, 0x28); //backlight
    prvLcdGpioInit();
    osiThreadSleep(1);

    RES(1);
    Delay(10);
    RES(0);
    Delay(10);
    RES(1);
    Delay(5);

    #if 1
    prv_I2c_write_com(0xE2);         //initialize interal function
    prv_I2c_write_com(0x2F);         //power control(VB,VR,VF=1,1,1)
     Delay(20);
    prv_I2c_write_com(0xA3);         //set LCD bias=1/5    33d
    prv_I2c_write_com(0x23);         //Regulator resistor select
    prv_I2c_write_com(0x81);         //set reference voltage
    prv_I2c_write_com(0x15);          // 0x15 =7.0v
    prv_I2c_write_com(0xC0);         //set SHL COM1 to COM64
    prv_I2c_write_com(0xA0);         //ADC select SEG1 to SEG132
    prv_I2c_write_com(0x40);         //Initial Display Line
    prv_I2c_write_com(0xA6);         //set reverse display OFF
    prv_I2c_write_com(0xAF);         //turns the display ON
    #else
    prv_I2c_write_com(0xE2);         //initialize interal function
    prv_I2c_write_com(0x2F);         //power control(VB,VR,VF=1,1,1)
     Delay(20);
    prv_I2c_write_com(0xA2);         //set LCD bias=1/5    33d
    prv_I2c_write_com(0xA0);         //Regulator resistor select
    prv_I2c_write_com(0xc0);         //set reference voltage
    prv_I2c_write_com(0x24);          // 0x15 =7.0v
    prv_I2c_write_com(0x81);         //set SHL COM1 to COM64
    prv_I2c_write_com(0x20);         //ADC select SEG1 to SEG132
    prv_I2c_write_com(0x2c);         //Initial Display Line
    prv_I2c_write_com(0x2e);         //set reverse display OFF
    prv_I2c_write_com(0x2f);         //turns the display ON
    prv_I2c_write_com(0xAF);
    #endif
    Delay(20);

    prv_I2c_write_com(0xA5);         //turns the display ON
    Delay(10);
    prv_I2c_write_com(0xA4);

    return 0;
}



#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
static uint8_t *g_seg_pin_map = NULL;
static uint8_t *g_ram_data = NULL;
static uint8_t g_num_len = 7;
static uint8_t g_num_len_extra = 3;
static uint8_t g_seg_len = 0;
//icon
static uint8_t g_num_max = 0;
static uint8_t g_signal_icon_level_max = 0;
static uint8_t g_battery_icon_level_max = 0;
static uint8_t g_wifi_icon_level_max = 0;
static uint8_t g_volume_icon_level_max = 0;
// static uint8_t g_wifi_icon_off = 0;
static uint8_t g_signal_icon_off = 0;

static uint8_t *g_num_icon_index = NULL;
static uint8_t g_point1_icon_index = 0;
static uint8_t g_point2_icon_index = 0;
static uint8_t g_point3_icon_index = 0;
static uint8_t g_4g_icon_index = 0;
static uint8_t g_wifi_icon_index = 0;
static uint8_t g_gw_strength_index = 0;
static uint8_t g_battery_icon_index = 0;
static uint8_t g_volume_icon_index = 0;
/*
*             bit0
*            -------
*      bit5 |       | bit1a
*           | bit6  |
*            -------
*      bit4 |       | bit2
*           | bit3  |
*            -------
* */
#define SEG_NUM_INDEX 7
static uint8_t seg_map[] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F // 9
};

static int seg_read_number_count(void)
{
    return 7;
}

int seg_get_number_count(void)
{
    return g_num_len;  //8
}

void seg_param_init(void)
{
    //根据不同的段数配置全局数组seg_pin_map[]
    uint8_t seg_8_pin_map_and[] = {
        7, (7 + 32 * 2), (7 + 32 * 4), (7 + 32 * 6), (6 + 32 * 6), (6 + 32 * 2), (6 + 32 * 4), //pin of fourth number
        9, (9 + 32 * 2), (9 + 32 * 4), (9 + 32 * 6), (8 + 32 * 6), (8 + 32 * 2), (8 + 32 * 4), //pin of third number
        11, (11 + 32 * 2), (11 + 32 * 4), (11 + 32 * 6), (10 + 32 * 6), (10 + 32 * 2), (10 + 32 * 4), //pin of second number
        13, (13 + 32 * 2), (13 + 32 * 4), (13 + 32 * 6), (12 + 32 * 6), (12 + 32 * 2), (12 + 32 * 4), //pin of first number
        
        
        1, (1 + 32 * 2), (1 + 32 * 4), (1 + 32 * 6), (0 + 32 * 6), (0 + 32 * 2), (0 + 32 * 4), //pin of seventh number
        3, (3 + 32 * 2), (3 + 32 * 4), (3 + 32 * 6), (2 + 32 * 6), (2 + 32 * 2), (2 + 32 * 4), //pin of sixth number
        5, (5 + 32 * 2), (5 + 32 * 4), (5 + 32 * 6), (4 + 32 * 6), (4 + 32 * 2), (4 + 32 * 4), //pin of fifth number
        
        
        29, (28 + 32 * 0), (28 + 32 * 4), (29 + 32 * 6), (29 + 32 * 4), (29 + 32 * 2), (28 + 32 * 2), //pin of tenth number
        27, (26 + 32 * 0), (26 + 32 * 4), (27 + 32 * 6), (27 + 32 * 4), (27 + 32 * 2), (26 + 32 * 2), //pin of tenth number
        25, (24 + 32 * 0), (24 + 32 * 4), (25 + 32 * 6), (25 + 32 * 4), (25 + 32 * 2), (24 + 32 * 2), //pin of tenth number
        23, (22 + 32 * 0), (22 + 32 * 4), (23 + 32 * 6), (23 + 32 * 4), (23 + 32 * 2), (22 + 32 * 2), //pin of tenth number
        21, (20 + 32 * 0), (20 + 32 * 4), (21 + 32 * 6), (21 + 32 * 4), (21 + 32 * 2), (20 + 32 * 2), //pin of tenth number
        19, (18 + 32 * 0), (18 + 32 * 4), (19 + 32 * 6), (19 + 32 * 4), (19 + 32 * 2), (18 + 32 * 2), //pin of tenth number
        17, (16 + 32 * 0), (16 + 32 * 4), (17 + 32 * 6), (17 + 32 * 4), (17 + 32 * 2), (16 + 32 * 2), //pin of tenth number
        15, (14 + 32 * 0), (14 + 32 * 4), (15 + 32 * 6), (15 + 32 * 4), (15 + 32 * 2), (14 + 32 * 2), //pin of tenth number
    
        (10 + 32 * 0), 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //pin of point1
    
        2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //pin of icon 4g
        0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //pin of icon wifi
    
        0xff,(31 + 32 * 6), (31 + 32 * 4), (31 + 32 * 2), (31 + 32 * 0), (4), 0xff, //GW strength
        (30 + 32 * 6), (30 + 32 * 2), (30 + 32 * 0), (30 + 32 * 4), 0xff, 0xff, 0xff, //pin of icon battery

        (18 + 32 * 6), 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //pin of point1
        (16 + 32 * 6), 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //pin of point1
    };

    int map_len = 0;
    switch (seg_read_number_count())
    {
    case 6:
        break;
    case 7:
        map_len = ARRAY_SIZE(seg_8_pin_map_and);
        g_seg_pin_map = malloc(map_len);
        memcpy(g_seg_pin_map, seg_8_pin_map_and, map_len);
        g_num_len = 8;
        g_num_len_extra = 7;
        g_seg_len = 32;
        g_num_icon_index = malloc(g_num_len + g_num_len_extra);
        memset(g_num_icon_index, 0, g_num_len + g_num_len_extra);
        for (int i = 0; i < g_num_len + g_num_len_extra; i++) g_num_icon_index[i] = SEG_NUM_INDEX * i;
        g_point1_icon_index = SEG_NUM_INDEX * (g_num_len + g_num_len_extra);
        g_point2_icon_index = SEG_NUM_INDEX * (g_num_len + g_num_len_extra + 5);
        g_point3_icon_index = SEG_NUM_INDEX * (g_num_len + g_num_len_extra + 6);
        g_4g_icon_index = SEG_NUM_INDEX * (g_num_len + g_num_len_extra + 1);
        g_wifi_icon_index = SEG_NUM_INDEX * (g_num_len + g_num_len_extra + 2);
        g_gw_strength_index = SEG_NUM_INDEX * (g_num_len + g_num_len_extra + 3);
        g_battery_icon_index = SEG_NUM_INDEX * (g_num_len + g_num_len_extra + 4);
        g_num_max = g_num_icon_index[g_num_len + g_num_len_extra - 1];
        g_signal_icon_level_max = FRAMEWORK_ICON_LEVEL5;
        g_battery_icon_level_max = FRAMEWORK_ICON_LEVEL3;
        g_wifi_icon_level_max = FRAMEWORK_ICON_LEVEL5;
        g_wifi_icon_level_max = 0xff;
        g_signal_icon_off = 0xff;
        break;
    case 8:
        break;
    default:
        break;
    }

    g_ram_data = malloc(g_seg_len);
    memset(g_ram_data, 0, g_seg_len);
    OSI_LOGI(TAG, "g_num_len %d g_seg_len %d g_num_max %d",g_num_len, g_seg_len, g_num_max);
}



typedef struct {
    drvLcdMoudleID_t lcd_id;
    drvLcd_t *lcd_dev;
    bool init_status;
    volatile bool backlight_status;
} seg_lcd_hal_t;

static seg_lcd_hal_t lcd_config = {0};

static osiMutex_t *lcd_seg_mutex = NULL;
static void seg_lcd_hal_lock(void)
{
    if (lcd_seg_mutex == NULL) {
        return;
    }

    osiMutexLock(lcd_seg_mutex);
}

static void seg_lcd_hal_unlock(void)
{
    if (lcd_seg_mutex == NULL) {
        return;
    }

    osiMutexUnlock(lcd_seg_mutex);
}

static void seg_lcd_hal_clear(void)
{
    uint8_t *tmp = NULL;
    tmp = (uint8_t *)malloc(g_seg_len * sizeof(uint8_t));
    memset(tmp, 0x00, g_seg_len * sizeof(uint8_t));
    // prvLcdIomuxConfig();
    // seg_lcd_hal_lock();
    prv_I2c_write_com(0xb0); //set page address
    prv_I2c_write_com((0x10)); //set high 4bit column address
    prv_I2c_write_com((0x00)); //set low  4bit column address

    //OSI_LOGI(TAG, "lcd_config.lcd_dev %p", lcd_config.lcd_dev);

    for (int i = 0; i < g_seg_len; i++) {
        prv_I2c_write_data(tmp[i]);
    }
    // seg_lcd_hal_unlock();
    free(tmp);
}

static uint8_t seg_lcd_hal_read_data(uint8_t *map)
{
    uint8_t data = 0;

    for (int i = 0; i < 7; i++) {
        if (map[i] != 0xff) {
            uint8_t offset = 0, index = 0;
            offset = map[i] % g_seg_len;
            if (g_num_len == 7) {
                index = map[i] / g_seg_len;
            } else {
                index = 0;
            }
            if (g_ram_data[offset] & (1 << index)) {
                data |= (1 << i);
            } else {
                data &= ~(1 << i);
            }

            // OSI_LOGI(TAG, "g_ram_data[offset] %d  offset %d index %d", g_ram_data[offset], offset, index);
        }
    }

    // OSI_LOGI(TAG, "data %d", data);
    return data;
}

void seg_lcd_hal_write_data(uint8_t *map, uint8_t data)
{
    seg_lcd_hal_lock();

    for (int i = 0; i < 7; i++) {
        if (map[i] != 0xff) {
            uint8_t offset = 0, index = 0;
            offset = map[i] % g_seg_len;
            // if (g_num_len == 7) {
                index = map[i] / g_seg_len;
            // } else {
            //     index = 0;
            // }
            if ((data >> i) & 0x01) {
                g_ram_data[offset] |= (1 << index);
            } else {
                g_ram_data[offset] &= ~(1 << index);
            }

            // OSI_LOGI(TAG, "%d g_ram_data[offset] %d  offset %d index %d", i, g_ram_data[offset], offset, index);
        }
    }

    seg_lcd_hal_unlock();
}

int seg_lcd_hal_read_num_length(void)
{
    int num = 0;

    for (int i = 0; i < g_num_len; i++) {
        if (0 != seg_lcd_hal_read_data(&g_seg_pin_map[g_num_max - i *SEG_NUM_INDEX])) {
            num++;
        }
    }

    return num;
}

void seg_lcd_hal_clear_num(void)
{
    for (int i = 0; i < g_num_len; i++) {
        seg_lcd_hal_write_data(&g_seg_pin_map[g_num_max - i *SEG_NUM_INDEX], 0);
    }
}

void seg_lcd_hal_clear_num_extra(void)
{
    if (g_num_len_extra == 0) return;
    for (int i = 4; i < 7; i++) {
        seg_lcd_hal_write_data(&g_seg_pin_map[SEG_NUM_INDEX *i], 0);
    }
}

void seg_lcd_hal_clear_num_time(void)
{
    if (g_num_len_extra == 0) return;
    for (int i = 0; i < 4; i++) {
        seg_lcd_hal_write_data(&g_seg_pin_map[SEG_NUM_INDEX *i], 0);
    }
}

void seg_lcd_hal_clear_point(void)
{
    seg_lcd_hal_write_data(&g_seg_pin_map[g_point1_icon_index], 0);
    // seg_lcd_hal_write_data(&g_seg_pin_map[g_point2_icon_index], 0);
}

void seg_lcd_hal_clear_pointnum(void)
{
    // seg_lcd_hal_write_data(&g_seg_pin_map[g_point1_icon_index], 0);
    seg_lcd_hal_write_data(&g_seg_pin_map[g_point2_icon_index], 0);
    seg_lcd_hal_write_data(&g_seg_pin_map[g_point3_icon_index], 0);
}



int framework_lcd7567_Init(void)
{
    if(lcd_config.init_status){
        return -1;
    }
    if (lcd_seg_mutex == NULL) {
        lcd_seg_mutex = osiMutexCreate();
    }
    seg_param_init();
    prv_lcd7567_init();
    seg_lcd_hal_clear();
    lcd_config.init_status = true;

    return 0;
}

void framework_lcd7567_ALLflush(void)
{
    seg_lcd_hal_lock();
    prv_I2c_write_com(0xb0);//set page address
    prv_I2c_write_com(0x10); //set high 4bit column address
    prv_I2c_write_com(0x00); //set low  4bit column address
    for (int i = 0; i < g_seg_len; i++) {
        prv_I2c_write_data(g_ram_data[i]);
    }
    seg_lcd_hal_unlock();
}


void framework_lcd7567_full_All_display(bool value)
{
    uint8_t *tmp = NULL;
    tmp = (uint8_t *)malloc(g_seg_len);
    seg_lcd_hal_lock();
    if (value != 0) {
        memset(tmp, 0xff, g_seg_len);
    } else {
        memset(tmp, 0x00, g_seg_len);
    }

    prv_I2c_write_com(0xb0);//set page address
    prv_I2c_write_com(0x10); //set high 4bit column address
    prv_I2c_write_com(0x00); //set low  4bit column address
    for (int i = 0; i < 32; i++) {
        prv_I2c_write_data(tmp[i]);
    }
    seg_lcd_hal_unlock();
}

/*4G icon*/
void framework_lcd7567_cellnet_ctrl(bool value)
{
    uint8_t temp = 0;

    temp = value;
    seg_lcd_hal_write_data(&g_seg_pin_map[g_4g_icon_index], temp);
}

void framework_lcd7567_startup_display(uint8_t index)
{
    if(index > 0 && index < 9){
        seg_lcd_hal_write_data(&g_seg_pin_map[SEG_NUM_INDEX * (g_num_len_extra + index - 1) ], (1 << 6));
    }else if(index == 0){
        for(int i = 1; i < 9; i++){
            seg_lcd_hal_write_data(&g_seg_pin_map[SEG_NUM_INDEX * (g_num_len_extra + i - 1) ], 0);
        }
    }
}

/*battery icon*/
void framework_lcd7567_battery_ctrl(uint8_t value, uint8_t level)
{
    static uint8_t prvvalue = 0;
    static uint8_t prvlevel = 0;
    uint8_t temp = 0;

    if (value != 0xff) {
        prvvalue = value;
    }

    if (prvvalue == false) {
        seg_lcd_hal_write_data(&g_seg_pin_map[g_battery_icon_index], 0);
        return;
    }

    if (level != 0xff) {
        prvlevel = level;
    }

    if (prvlevel > g_battery_icon_level_max) {
        temp = g_battery_icon_level_max;
    } else {
        temp = prvlevel;
    }

    seg_lcd_hal_write_data(&g_seg_pin_map[g_battery_icon_index], temp);
}

/*wifi icon*/
void framework_lcd7567_wifi_ctrl(bool value)
{
    uint8_t temp = 0;

    temp = value;
    seg_lcd_hal_write_data(&g_seg_pin_map[g_wifi_icon_index], temp);
}

/*信号强度*/
void framework_lcd7567_sign_ctrl(uint8_t value, uint8_t level)
{
    // if (g_num_len == 7) {
        uint8_t temp = 0;

        // OSI_LOGI(TAG, "sign value = %d,level = %d.",value,level);
        if (value == 0x0) {
            seg_lcd_hal_write_data(&g_seg_pin_map[g_gw_strength_index], 0);
            return;
        }
        if (level == 0xff) {
            temp = 0;
        } else if (level > g_signal_icon_level_max) {
            temp = g_signal_icon_level_max;
        } else {
            temp = level;
        }
        seg_lcd_hal_write_data(&g_seg_pin_map[g_gw_strength_index], temp);
    // }
}

void framework_lcd7567_volume_ctrl(uint8_t value, uint8_t level)
{
    if (g_num_len == 6 || g_num_len == 8) {
        static uint8_t prvvalue = 0;
        static uint8_t prvlevel = 0;
        uint8_t temp = 0;

        if (value != 0xff) {
            prvvalue = value;
        }

        if (prvvalue == false) {
            seg_lcd_hal_write_data(&g_seg_pin_map[g_volume_icon_index], 0);
            return;
        }

        if (level != 0xff) {
            prvlevel = level;
        }

        if (prvlevel > g_volume_icon_level_max) {
            temp = g_volume_icon_level_max;
        } else {
            temp = prvlevel;
        }

        seg_lcd_hal_write_data(&g_seg_pin_map[g_volume_icon_index], temp);
    }
}

#define MAX_CHAR_MAP_SIZE  128
typedef struct {
    char key; /*a b c d...*/
    uint8_t value; /*a 0b01110111 ...*/
} CharMap;

static CharMap char_map[MAX_CHAR_MAP_SIZE];
static uint8_t char_map_size = 0;

void seg_lcd_hal_ntd_set_disp_map(char key, uint8_t value)
{
    for (uint8_t i = 0; i < char_map_size; i++) {
        if (char_map[i].key == key) {
            char_map[i].value = value;
            return;
        }
    }

    if (char_map_size < MAX_CHAR_MAP_SIZE) {
        char_map[char_map_size].key = key;
        char_map[char_map_size].value = value;
        char_map_size++;
    } else {
        OSI_LOGI(TAG, "Char map is full, cannot add more characters.");
    }
}

void framework_lcd7567_ntd_display(char *str)
{
    uint8_t *seg_data = NULL;
    seg_data = malloc(g_num_len);
    uint8_t seg_point[2] = {0};
    int len = strlen(str);
    int numlen = 0;
    int point_posi = 0xff,piontidx = 0xff;
    for (int i = 0; i < len && numlen < g_num_len; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            seg_data[numlen] = seg_map[str[i] - '0'];
            // OSI_LOGI(TAG, "i %d seg_idx %d seg_data[seg_idx] 0x%x  '%c'", i, numlen, seg_data[numlen], str[i]);
            numlen++;
        } else if (str[i] == '.') {
            point_posi = numlen;
        } else {
            for (uint8_t j = 0; j < char_map_size; j++) {
                if (char_map[j].key == str[i]) {
                    seg_data[numlen] = char_map[j].value;
                    break;
                }
            }
            numlen++;
        }
    }

    //point
    if (point_posi != 0xff) {
        piontidx = numlen - point_posi;

        if (piontidx == 1) { //p2
            seg_point[0] = 0x00;
            seg_point[1] = 0x01;
        } else if (piontidx == 2) { //p1
            seg_point[0] = 0x01;
            seg_point[1] = 0x00;
        } else {
            seg_point[0] = 0x00;
            seg_point[1] = 0x00;
        }
    } else {
        seg_point[0] = 0x00;
        seg_point[1] = 0x00;
    }

    // OSI_LOGI(TAG, "numlen %d", numlen);
    //clear
    seg_lcd_hal_clear_num();
    seg_lcd_hal_clear_pointnum();

    //num disp
    for (int i = 0; i < numlen; i++) {
        seg_lcd_hal_write_data(&g_seg_pin_map[g_num_max - i *SEG_NUM_INDEX], seg_data[numlen - i - 1]);
    }

    seg_lcd_hal_write_data(&g_seg_pin_map[g_point2_icon_index], seg_point[0]);
    seg_lcd_hal_write_data(&g_seg_pin_map[g_point3_icon_index], seg_point[1]);
    
    free(seg_data);
}


void framework_lcd7567_extraNum_display(char *str)
{
    if (g_num_len_extra == 0) return;
    uint8_t *seg_data = NULL;
    seg_data = malloc(g_num_len_extra);
    // uint8_t seg_point[2] = {0};
    int len = strlen(str);
    int numlen = 0;

    for (int i = 0; i < len && numlen < g_num_len_extra; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            seg_data[numlen] = seg_map[str[i] - '0'];
            // OSI_LOGI(TAG, "i %d seg_idx %d seg_data[seg_idx] 0x%x  '%c'", i, numlen, seg_data[numlen], str[i]);
            numlen++;
        } else {
            for (uint8_t j = 0; j < char_map_size; j++) {
                if (char_map[j].key == str[i]) {
                    seg_data[numlen] = char_map[j].value;
                    break;
                }
            }
            numlen++;
        }
    }

    // OSI_LOGI(TAG, "piontidx %d", piontidx);
    //clear
    seg_lcd_hal_clear_num_extra();

    //num disp
    for (int i = 0; i < numlen; i++) {
        seg_lcd_hal_write_data(&g_seg_pin_map[g_num_icon_index[6] - i *SEG_NUM_INDEX], seg_data[numlen - i - 1]);
    }

    free(seg_data);
}

void framework_lcd7567_time_display(char *str)
{
    if (g_num_len_extra == 0) return;
    uint8_t *seg_data = NULL;
    seg_data = malloc(4);
    // uint8_t seg_point[2] = {0};
    int len = strlen(str);
    int numlen = 0;

    for (int i = 0; i < len && numlen < 4; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            seg_data[numlen] = seg_map[str[i] - '0'];
            // OSI_LOGI(TAG, "i %d seg_idx %d seg_data[seg_idx] 0x%x  '%c'", i, numlen, seg_data[numlen], str[i]);
            numlen++;
        } else {
            for (uint8_t j = 0; j < char_map_size; j++) {
                if (char_map[j].key == str[i]) {
                    seg_data[numlen] = char_map[j].value;
                    break;
                }
            }
            numlen++;
        }
    }

    // OSI_LOGI(TAG, "piontidx %d", piontidx);
    //clear
    seg_lcd_hal_clear_num_time();
    seg_lcd_hal_write_data(&g_seg_pin_map[g_point1_icon_index], 1);
    //num disp
    for (int i = 0; i < numlen; i++) {
        seg_lcd_hal_write_data(&g_seg_pin_map[g_num_icon_index[3] - i *SEG_NUM_INDEX], seg_data[numlen - i - 1]);
    }

    free(seg_data);
}


/*backlight*/
extern void drvIsinkPwmEnable(bool enabled, char level, char duty);
#define CONFIG_MOD_LCD_TYPE_SEG_LCD
#if defined CONFIG_MOD_LCD_TYPE_RGB_LCD || defined CONFIG_MOD_LCD_TYPE_SEG_LCD
void lcd_hal_set_backlight(bool enable)
{
    // OSI_LOGI(TAG, "enter %d", enable);

    if (enable) {
#ifdef CONFIG_MOD_LCD_TYPE_SEG_LCD
        framework_lcd7567_ALLflush();
#endif
        // appSetsleepVote(0x3, false);
        lcd_config.backlight_status = true;
    } else {
#ifdef CONFIG_MOD_LCD_TYPE_SEG_LCD
        seg_lcd_hal_clear();
#endif
        // appSetsleepVote(0x3, true); // open sleep when off backlight
        lcd_config.backlight_status = false;
    }

    #ifdef CONFIG_MOD_LCD_TYPE_SEG_LCD
    drvIsinkPwmEnable(enable, 3, 0x28); //backlight
    #else
    drvLcdSetBackLightEnable(lcd_config.lcd_dev, enable);
    #endif

    // OSI_LOGI(TAG, "leave");
}

int lcd_hal_get_backlight_status(void)
{
    return lcd_config.backlight_status;
}
#endif

static int lcd_backlight_time = 10;
static osiTimer_t *lcd_timer = NULL;
int framework_lcd7567_get_backlight_time(void)
{
    return lcd_backlight_time;
}

int framework_lcd7567_set_backlight(uint8_t en)
{
    // SDK_LOG_V("en %d", en);
    lcd_hal_set_backlight(en);
    return 0;
}

int framework_lcd7567_get_backlight_status(void)
{
    return lcd_hal_get_backlight_status();
}

static void callcack_backlight_timeout(void *ctx)
{
    (void)ctx;
    framework_lcd7567_set_backlight(false);
    (void)osiTimerStop(lcd_timer);
    osiTimerDelete(lcd_timer);
    lcd_timer = NULL;
    OSI_LOGI(TAG, "timeout! Turn off the backlight!");
    return ;
}

int framework_lcd7567_set_backlight_time(int second)
{
    if (second == -1) { //release timer
        if (lcd_timer != NULL) {
            (void)osiTimerStop(lcd_timer);
            osiTimerDelete(lcd_timer);
            lcd_timer = NULL;
        }
        return 0;
    }

    if ((second < 5) || (second > 2000)) {
        OSI_LOGI(TAG, "time is out off range(5~2000)!");
        return -1;
    }

    // changge lcd backlight time value
    lcd_backlight_time = second;

    //set timeout
    if (NULL == lcd_timer) {
        // lcd_hal_set_backlight(true);
        framework_lcd7567_set_backlight(true);
        lcd_timer = osiTimerCreate(callcack_backlight_timeout, NULL);
        osiTimerStart(lcd_timer, second * 1000);
        OSI_LOGI(TAG, "timeout is %dS!", second);
    } else {
        (void)osiTimerStop(lcd_timer);
        osiTimerStart(lcd_timer, second * 1000);
        OSI_LOGI(TAG, "timer is running!");
    }

    return 0;
}
