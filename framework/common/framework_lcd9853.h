#ifndef _FREAMEWORK_LCD9853_H
#define _FRAMEWORK_LCD9853_H
/**
 * @brief       此模块用于描述 <strong>LCD模块</strong> 相关的接口
 * @note
 * @par 文件    lcd.h
 * @author
 * @date        2018/11/09
 * @version     1.0.0
 * @remarks        Change History:
 * Date          | Author          | Desc
 * ------------- | --------------- | ----
 * 2018/11/09    | Jezz Lin        | Create file
*/

/**
 * @defgroup LCD  LCD
 * @{
*/
// caution, doxygen defgroup header and footer and been both auto generated!!!

#ifndef __LCD_H__
#define __LCD_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include "osi_api.h"
// #include "linuxtypes.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** 是否有必要导出到应用层? */
#define     SCREEN_DIR_0            0           /**< LCD的显示方向，正向输出 */
#define     SCREEN_DIR_180          1           /**< LCD的显示方向，翻转180度输出 */
#define     SCREEN_DIR_90           2           /**< LCD的显示方向，翻转180度输出 */
#define     SCREEN_DIR_270          3           /**< LCD的显示方向，翻转180度输出 */

#define     SCR_WIDTH               ({uint32_t w;     \
                                        uint32_t h;   \
                                        uint32_t b;   \
                                        lcd_get_size(&h, &w, &b);   \
                                        w;})    /**< LCD的宽度像素 */
#define     SCR_HEIGHT              ({uint32_t w;     \
                                        uint32_t h;   \
                                        uint32_t b;   \
                                        lcd_get_size(&h, &w, &b);   \
                                        h;})    /**< LCD的高度像素 */

// lcdDisplay显示模式
#define     DISP_ASCII              (0   )      /**< 小字体显示(5*7) */
#define     DISP_CFONT              (1<<0)      /**< 大字体显示(8*16) */
#define     DISP_CLRLINE            (1<<5)      /**< 清除显示占用的行 */
#define     DISP_MEDIACY            (1<<6)      /**< 居中显示 */
#define     DISP_REVERSE            (1<<7)      /**< 反白显示 */
#define     DISP_INVLINE            (1<<4)      /**< 整行反白显示 */
#define     DISP_FLIP               (1<<8)      /**< 当行显示也执行flip */

// 图标控制
#define     ICON_PHONE              1           /**< phone 电话 */
#define     ICON_SIGNAL             2           /**< wireless signal 信号 */
#define     ICON_PRINTER            3           /**< printer 打印机 */
#define     ICON_ICCARD             4           /**< smart card IC卡 */
#define     ICON_LOCK               5           /**< lock 锁 */
#define     ICON_BATTERY            6           /**< 电池图标 */
#define     ICON_UP                 7           /**< up 向上 */
#define     ICON_DOWN               8           /**< down 向下 */
#define     ICON_LINK               9           /**< 连接图标 */
#define     ICON_WIFI               10          /**< wifi图标 */
#define     ICON_VOLUME             11          /**< 声音图标 */
#define     CLOSEICON               0           /**< 关闭图标[针对所有图标] */
#define     OPENICON                1           /**< 显示图标[针对打印机、IC卡、锁、电池、向上、向下] */

// 背光模式
#define     LIGHT_OFF_TEMP          0           /**< 背光临时关闭(对于定时模式用) */
#define     LIGHT_TIMER_MODE        1           /**< 背光定时模式 */
#define     LIGHT_ON_MODE           2           /**< 背光常开模式 */
#define     LIGHT_OFF_MODE          3           /**< 背光常关模式 */

#ifndef ICON_OFF
#define ICON_OFF                    0
#define ICON_ON                     !ICON_OFF
#endif

#define UP_ICON                     122
#define DOWN_ICON                   126

#define LINE_WRAP                   1 //自动换行
#define NO_WRAP                     0 //非自动换行

#define LCD_OK                      0
#define LCD_FAIL                   -1


#define LCD_TRANSFORM_NONE       0x00  // 无变换
#define LCD_TRANSFORM_ROTATE_90  0x01  // 旋转90度
#define LCD_TRANSFORM_ROTATE_180 0x02  // 旋转180度
#define LCD_TRANSFORM_ROTATE_270 0x03  // 旋转270度
#define LCD_TRANSFORM_MIRROR_X   0x04  // X轴镜像
#define LCD_TRANSFORM_MIRROR_Y   0x08  // Y轴镜像

#define LCD_IS_ROTATE_90(mask)   ((mask & LCD_TRANSFORM_ROTATE_90) == LCD_TRANSFORM_ROTATE_90)
#define LCD_IS_ROTATE_180(mask)  ((mask & LCD_TRANSFORM_ROTATE_180) == LCD_TRANSFORM_ROTATE_180)
#define LCD_IS_ROTATE_270(mask)  ((mask & LCD_TRANSFORM_ROTATE_270) == LCD_TRANSFORM_ROTATE_270)
#define LCD_IS_MIRROR_X(mask)    ((mask & LCD_TRANSFORM_MIRROR_X) == LCD_TRANSFORM_MIRROR_X)
#define LCD_IS_MIRROR_Y(mask)    ((mask & LCD_TRANSFORM_MIRROR_Y) == LCD_TRANSFORM_MIRROR_Y)

typedef enum {
    //GPRS信号图标索引
    ICON_WIRELESS_SIGNAL_1 = 0,
    ICON_WIRELESS_SIGNAL_2,
    ICON_WIRELESS_SIGNAL_3,
    ICON_WIRELESS_SIGNAL_4,
    ICON_WIRELESS_SIGNAL_5,
    ICON_WIRELESS_SIGNAL_NONE,
    /* 数据连接图标索引 */
    ICON_WIRELESS_LINK,
    //LED图标索引
    ICON_LED_BLANK, //1
    ICON_LED_BLANK1, //2
    ICON_LED_BLANK2, //3
    ICON_LED_BLANK3, //4
    ICON_LED_BLUE, //1
    ICON_LED_YELLOW, //2
    ICON_LED_GREEN, //3
    ICON_LED_RED, //4
    //IC卡图标索引
    ICON_ICCARD_0,
    //LOCK图标索引
    ICON_PED_LOCK,
    //WIFI图标索引
    ICON_WIFI_1,
    ICON_WIFI_2,
    ICON_WIFI_3,
    ICON_WIFI_4,
    ICON_WIFI_UNLINK,
    //上/下翻图标索引
    ICON_MENU_UP_DOWN,
    ICON_MENU_UP,
    ICON_MENU_DOWN,
    //BATTERY图标索引
    ICON_BATTERY_0,
    ICON_BATTERY_1,
    ICON_BATTERY_2,
    ICON_BATTERY_3,
    ICON_BATTERY_4,
    ICON_BATTERY_CHARGE,
    ICON_BATTERY_CHARGE_FINISH,
    ICON_BATTERY_ERROR,
    //TIME图标索引
    ICON_TIME_COLON,
    ICON_TIME_0,
    ICON_TIME_1,
    ICON_TIME_2,
    ICON_TIME_3,
    ICON_TIME_4,
    ICON_TIME_5,
    ICON_TIME_6,
    ICON_TIME_7,
    ICON_TIME_8,
    ICON_TIME_9,
    ICON_BLE,
    //背景图标索引
    ICON_BACKGROUND,
    //WIFI路由器图标索引
    ICON_WIFI_AP_ROUTER,
    //开发版本图标索引
    ICON_DEVELOPMENT,
    //新固件英文图标索引
    ICON_NEW_FW_EN,
    //新固件中文图标索引
    ICON_NEW_FW_CN,
    ICON_INDEX_END,
} icon_t;


#define EXT_FLASH_SELEXT 1

typedef void (*lcdFlipComplete)(void);
typedef void *font_t; /**< font指针类型定义  */

/**
 * @brief  设置图标显示关闭
 * @param[in]  icon_no: 需要控制的图标
 * @param[in]  mode:
                     - 1 显示
                     - 0 关闭
 * @retval none
 * @note
 */
void lcd_set_icon(int icon_no, int mode);

/**
 * @brief  刷新显示内容
 * @retval none
 * @note
 */
void lcd_flip(void);
/**
 * @brief  刷新系统栏
 * @note   自定义接口
 * @retval None
 */
void lcd_flip_icon(void);
/**
 * @brief  同步刷新显示内容
 * @retval  -1  失败
 * @retval  0   成功
 * @note
 */
int lcd_flip_sync(void);
/**
 * @brief  获取多图层
 * @note   自定义接口
 * @retval  0: paint; 1: camera; 2:sys msg
 * @note
 */
uint8_t lcd_get_multi_layer(void);

/**
 * @brief  使能多图层
 * @note   自定义接口
 * @param[in]   0: disable 1, 1: enable, 2: add system message layer
 * @retval  -1  失败
 * @retval  0   成功
 * @note
 */
int lcd_set_multi_layer(uint8_t en);

/**
 * @brief  获取lcd显示区域信息
 * @param[out]  height: 显示区域高
 * @param[out]  width: 显示区域宽
 * @param[out]  bits: 说明比特数/像素，其值为1、4、8、16、24
 * @retval none
 * @note
 */
int lcd_get_size(uint32_t *height, uint32_t *width, uint32_t *bits);
/**
 * @brief  获取屏幕显示状态栏的高度
 * @return
        - -1   失败，
        - > 0  成功，返回状态的高度值
 * @note   单位: 像素
 */
int lcd_get_status_bar_height(void);

/**
 * @brief  获取系统默认显示的font指针
 * @return
        - NULL    获取失败
        - 非NULL  成功，返回对应的font指针
 * @note
 */
void *lcd_get_sys_font(void);

/**
 * @brief  获取系统默认显示的paint指针
 * @return
        - NULL    获取失败
        - 非NULL  成功，返回对应的paint指针
 * @note
 */
void *lcd_get_sys_paint(void);

/**
 * @brief  获取状态栏icon显示的paint指针
 * @return
        - NULL    获取失败
        - 非NULL  成功，返回对应的paint指针
 * @note
 */
void *lcd_get_sys_paint_icon(void);

/**
 * @brief  获取系统栏状态
 * @note   自定义接口
 * @retval 系统栏状态
 */
uint16_t lcd_get_icon_status(void);

/**
 * @brief  获取屏幕背光时间
 * @return
        - uint16_t   屏幕背光时间
 * @note
 */
uint16_t lcd_get_backlight_time(void);

/**
 * @brief   获取LCD背光状态
 * @retval  1   LCD亮
 * @retval  0   LCD灭
 * @retval  -1  调用失败
 */
int lcd_get_backlight_status(void);

/**
 * @brief       设置屏幕背光时间
 * @param[in]   second  秒数(限制5 ~ 2000之间)
 * @retval      0       成功
 * @retval      -1      调用失败
 */
int lcd_set_backlight_time(int second);

/**
 * @brief  使能背光
 * @note   自定义接口
 * @param[in]   en  1-enable; 0-disable
 * @retval      0       成功
 * @retval      -1      调用失败
 */
int lcd_set_backlight(uint8_t en);

/**
 * @brief  获取屏幕除状态栏外的显示区域的缓存（起始）地址
 * @return
        - NULL    获取失败
        - 非NULL  成功，返回对应的缓存首地址
 * @note
 */
const uint8_t *lcd_get_buffer_addr(void);

/**
 * @brief  释放lcd_get_buffer_addr申请的空间
 * @note   自定义接口
 * @retval 0 成功
 * @retval -1 成功
 */
int lcd_free_buffer_addr(void);

/**
 * @brief  获取lcd旋转方向
 * @note   自定义接口
 * @retval ref define in lcd_config.h
 */
int lcd_get_direction(void);

/*color conversion*/
/**
 * @brief  rgb888转rgb566
 * @note   自定义接口
 * @param  color: rgb888 颜色 r|g|b
 * @retval rgb565颜色
 */
uint16_t rgb888_to_rgb565(uint32_t color);

/**
 * @brief  rgb565转rgb888
 * @note   自定义接口
 * @param  color: rgb565 颜色 r|g|b
 * @retval rgb888颜色
 */
uint32_t rgb565_to_rgb888(uint16_t color);

/**
 * @brief  rgb565颜色填充到lcd buffer中
 * @note   自定义接口
 * @param  color: rgb565颜色
 * @retval rgb565颜色
 */
uint16_t fill_color(uint16_t color);

/*buffer*/
/**
 * @brief  申请填充颜色的buffer
 * @note   自定义接口
 * @param  width: buffer width
 * @param  height: buffer width
 * @retval buffer address
 */
void *alloc_framebuffer_memory(int width, int height);

/**
 * @brief  释放buffer
 * @note   自定义接口
 * @param  handle: buffer address
 * @retval 0 sucess
 */
int free_framebuffer_memory(void *handle);

/**
 * @brief  lcd初始化
 * @note   自定义接口
 * @retval 0 sucess
 * @retval -1 fail
 */
int lcd_init();

/**
 * @brief  系统栏显示bmp图标
 * @note   自定义接口
 * @param  icon: ref icon_t in lcd_config.h
 * @retval None
 */
void show_icon(icon_t icon);

/**
 * @brief  系统栏显示样机字样
 * @note   自定义接口
 * @param  en: 1 show bmp; 0 show 0xffff;
 * @retval None
 */
void show_proto_icon(uint8_t en);

/**
 * @brief  字库异常时，重新打卡字库
 * @note   自定义接口
 * @retval
 */
int reopen_sys_font(void);

void lcd_check_sys_font(void);

void mem_lock(void);
int mem_try_lock(uint32_t timeout);
void mem_unlock(void);

int get_sys_ascii_size(void);
int support_icon(void);

/**************segment lcd **********************/
/**
* @brief 余额显示
* @param[in] mode:0 预留(断码屏上有单位的作为是否显示单位)
* @param[in]: str:余额（字符串格式） 如:”11.23”
* @retval none
*/
void ntd_display(uint32_t mode, char *str);

/**
* @brief 小数字显示
* @param[in] mode:0 预留
* @param[in]: str:字符串格式 如:”123”
* @retval none
*/
void ntd_display_num_extra(uint32_t mode, char *str);

/**
* @brief 获取断码屏数字的数量
* @retval none
*/
int ntd_get_number_count(void);

/*
*             bit0
*            -------
*      bit5 |       | bit1
*           | bit2  |
*            -------
*      bit3 |       | bit5
*           | bit4  |
*            -------
*
* @param[in] key:字符
* @param[in] value:该字符对应的显示
* @retval none
*/
void ntd_set_disp_map(char key, uint8_t value);

/**
 * @brief  强制全显示
 * @note
 * @param  value:1-全显示 0-不显示
 * @retval None
 */
void test_control_all_icon(uint8_t value);

int lcd_get_GW(void);

#ifdef __cplusplus
}

#endif /* __cplusplus */

#endif /*  __LCD_H__ */

/** @} */



typedef enum {
    /** normal direction */
    LCD_DIR_NORMAL =0,
    /** Y is inverted */
    LCD_DIR_YINV,
    /** X is inverted */
    LCD_DIR_XINV,
    /** both Y and X are inverted */
    LCD_DIR_YINV_XINV,
    /** X-Y are swapped */
    LCD_DIR_EXCHG,
    /** X-Y are swapped, Y is inverted */
    LCD_DIR_EXCHG_YINV,
    /** X-Y are swapped, X is inverted */
    LCD_DIR_EXCHG_XINV,
    /** X-Y are swapped, both X and Y are inverted */
    LCD_DIR_EXCHG_XINV_YINV,
    /** alias for rotate 0 degree */
    LCD_DIR_ROTATE_0 = LCD_DIR_NORMAL,
    /** alias for rotate 90 degree */
    LCD_DIR_ROTATE_90 = LCD_DIR_EXCHG_XINV,
    /** alias for rotate 180 degree */
    LCD_DIR_ROTATE_180 = LCD_DIR_YINV_XINV,
    /** alias for rotate 270 degree */
    LCD_DIR_ROTATE_270 = LCD_DIR_EXCHG_YINV,
} lcd_direction_t;

typedef struct {
    uint8_t spi_mode;
    void *data_buffer;
    uint32_t data_size; //unit:byte
    uint8_t sync; //1:同步 0:异步
    uint8_t transfer_mode; //0:dma 1:te
    void* funcCb;
    uint32_t width;
    uint32_t height;
    uint32_t dummy_header_size; //unit:byte
    uint32_t disp_res;
    uint32_t start_line; //0,1,...
} refer_param_t;

typedef struct{
    void *paint_handle;
    void *font_used_handle;
    uint32_t w;
    uint32_t h;
    uint32_t bits;
    osiWork_t *work;
    uint32_t backlight_timeout_ms;
}lcd_handle_t;

void lcd_hal_set_brightness(uint8_t val);
uint8_t lcd_hal_get_brightness(void);

int lcd_hal_init(void);
uint32_t lcd_hal_get_id(void);
bool lcd_hal_get_init_status(void);
void lcd_hal_get_size(uint32_t *height, uint32_t *width, uint32_t *bits);
void lcd_hal_config_screen(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_direction_t dir);
void lcd_hal_refresh_screen(void *data_buffer, uint32_t data_size, uint8_t sync);
void lcd_hal_refresh_screen_with_param(refer_param_t *refer_param);
void lcd_hal_refresh_done(void);
void cust_lcd_hal_set_backlight(bool enable);
int cust_lcd_hal_get_backlight_status(void);
void *alloc_framebuffer(int width, int height);
int free_framebuffer(void *handle);
uint16_t lcd_get_backlight_time(void);
int lcd_set_backlight_time(int second);

/* Customer can override to provide picture list for Display_picture */
const char *const *framework_get_bmp_paths(int *count);

void app_lcd_backlight_off(osiTimer_t* backlight_timer);
void app_lcd_backlight_on(void);
void lcd_start_work(void *ctx,osiTimer_t* backlight_timer,lcd_handle_t *g_lcd);
int app_lcd_backlight_timer_start(int timeout_ms,lcd_handle_t* g_lcd);

int framework_lcd9853_gpio_init(unsigned int SCL_PIN_MFP, unsigned int SCL_PIN_NUM, unsigned int SDA_PIN_MFP, unsigned int SDA_PIN_NUM);
int Display_string_lcd(font_t* font_handler,lcd_handle_t* g_lcd,int x,int y,int width,int heigth,int font_size,char *string);
int app_lcd_clear(int x,int y,int width,int heigth,lcd_handle_t *g_lcd);
int Display_picture(lcd_handle_t* g_lcd,int start_x,int start_y,int serial);
/* Default provided in framework; customers can override with their own strong symbol */
int customer_lcd_init(void);

#endif
