#include "framework_lcd9853.h"
#include "framework_lcd9853_paint.h"
#include "framework_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gpio_api.h"
#include "osi_api.h"
#include <time.h>
#include "drv_def.h"
#include "drv_lcd_id.h"
#include "drv_lcd.h"
#include "drv_lcd_panel.h"
#include "fs_api.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


#include "common_api.h"
/*LCD HAL*/
static uint8_t prv_brightness = 0x55;
static bool prv_brightness_en = true;
extern void drvIsinkPwmEnable(bool enabled, char level, char duty);

typedef struct {
    char *lcd_name;
    drvLcdMoudleID_t lcd_id;
    drvLcd_t *lcd_dev;
    drvLcdPanelDesc_t *dev_desc;
    drvLcdArea_t areas;
    lcd_direction_t direction;
    volatile bool backlight_status;
    volatile bool init_status;
} lcd_hal_t;

static lcd_hal_t lcd_config = {0};
#define BMP_CHUNK_LINES  32
#define MIN(a,b)         ((a) < (b) ? (a) : (b))
static uint16_t s_line_buf[240 * BMP_CHUNK_LINES];

/*LCD*/
#ifdef CONFIG_SYSTEM_FUNC_C_WRAP
#include "c_wrap.h"
#endif
#ifdef CONFIG_MOD_LVGL_V8
static int lcd_backlight_time = -1;
#else
static int lcd_backlight_time = 65;
#endif
static void callcack_backlight_timeout(void *ctx);
static osiTimer_t *lcd_timer = NULL;


typedef struct {
    uint16_t icon_x;
    uint16_t icon_y;
    uint16_t icon_width;
    uint16_t icon_height;
    uint16_t icon_back_color;
    void *iconBuff;
    void *back_iconBuff;
    void *mix_iconBuff;
} lcd_iconHandle_t;

typedef struct {
    uint16_t width;
    uint16_t height;
} lcd_paintRange_t;

typedef struct {
    int m_x;/**< 矩形起点X坐标 */
    int m_y;/**< 矩形起点Y坐标  */
    uint32_t width; /**<高度  */
    uint32_t height; /**<  宽度 */
} lcd_textRect_t;

typedef struct {
    lcd_paintRange_t paintPixel;
    lcd_textRect_t rectangle;
    int bits_per_pixel;
    uint16_t back_color; //rgb565
    uint16_t fore_color;
    void *colorBuff; //rgb565 send to lcd
    void *back_colorBuff;
    void *sys_msg_colorbuffer;//for system message
    void *buf_to_user; //rgb888
    osiSemaphore_t *sema_backbuf_lock;
} lcd_paintHandle_t;

typedef struct {
    uint16_t font_width;
    uint16_t font_height;
    uint16_t font_size;
    char *font_charset;
    int fd;
    int second_fd;
} lcd_fontHandle_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t area;
} lcd_refresh_area_t;

typedef struct {
    uint32_t id;
    int sw_screen_dir;
    lcd_direction_t hw_screen_dir;
    int sys_icon_height;
    lcd_refresh_area_t sys_icon_area;
    lcd_refresh_area_t paint_area;
} lcd_config_t;
char *FONT_CH = "D:/home/Charset_ascii_24_24.FON";
char *USED_FONT = NULL;
char *USED_ICON = NULL;
char *USED_ICON_CHAR = NULL;

static void *font_ch_handle;
static void *font_used_handle;
static void *paint_handle;
// static void *paint_icon_handle;
// static void *bmp_handle;
// static void *bmp_char_handle;

static void *g_lcd_buffer_addr = NULL;
static uint16_t g_icon_status = 0x0000;
static volatile uint8_t g_multi_layer = 0;
static lcd_fontHandle_t *g_sys_ascii_font_handle = NULL;
static int lcd_set_direction(void);
static osiSemaphore_t *sema_mem_lock = NULL;
static osiSemaphore_t *sema_flip_lock = NULL;
static osiMessageQueue_t *msgQ_async_flip = NULL;

static bool lcd_cam_preview = false;

static lcd_config_t g_lcd_config;

OSI_SECTION_RW_KEEP void mem_lock(void)
{
    osiSemaphoreAcquire(sema_mem_lock);
}

OSI_SECTION_RW_KEEP int mem_try_lock(uint32_t timeout)
{
    return (int)osiSemaphoreTryAcquire(sema_mem_lock, timeout);
}

OSI_SECTION_RW_KEEP void mem_unlock(void)
{
    osiSemaphoreRelease(sema_mem_lock);
}

OSI_SECTION_RW_KEEP void flip_lock(void)
{
    osiSemaphoreAcquire(sema_flip_lock);
    // mem_lock();
}

OSI_SECTION_RW_KEEP void flip_unlock(void)
{
    // mem_unlock();
    osiSemaphoreRelease(sema_flip_lock);
}

/*color conversion*/
/*
* RGB888       | RGB565
* R: 0xff0000  | R: 0xf800
* G：0x00ff00   | G: 0x07e0
* B: 0x0000ff  | B: 0x001f
*/
OSI_SECTION_RW_KEEP uint16_t rgb888_to_rgb565(uint32_t color)
{
    uint16_t r = (uint16_t)((color & 0xff0000) >> 16);
    uint16_t g = (uint16_t)((color & 0x00ff00) >> 8);
    uint16_t b = (uint16_t)(color & 0x0000ff);

    r >>= 3;
    g >>= 2;
    b >>= 3;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

OSI_SECTION_RW_KEEP uint32_t rgb565_to_rgb888(uint16_t color)
{
    uint32_t r = (uint32_t)((color & 0xf800) >> 11);
    uint32_t g = (uint32_t)((color & 0x07e0) >> 5);
    uint32_t b = (uint32_t)(color & 0x001f);

    r <<= 3;
    g <<= 2;
    b <<= 3;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

OSI_SECTION_RW_KEEP uint16_t fill_color(uint16_t color)
{
    uint32_t id = 0;
    id = lcd_hal_get_id();

    switch (id) {
        case 0x077353:
            return color;

        case 0x773532:
        case 0x307789: {
            uint16_t r = ((color & 0xf800) >> 11);
            uint16_t g = ((color & 0x07e0) >> 5);
            uint16_t b = (color & 0x001f);
            return (uint16_t)(r | (g << 5) | (b << 11));
        }

        case 0x007567:
        default:
            return color;
    }
}

/* support user paint area*/
OSI_SECTION_RW_KEEP int lcd_draw_rect(int x, int y, uint32_t width, uint32_t height, uint16_t buffer, const void *paint)
{
    SDK_LOG_V("enter");
    // mem_lock();
    lcd_hal_refresh_done();
    uint32_t h, w;
    lcd_get_size(&h, &w, NULL);
    uint16_t *colorBuff = (uint16_t *)((lcd_paintHandle_t *)paint)->colorBuff;

    switch (lcd_get_direction()) {
        case LCD_TRANSFORM_ROTATE_270: {
            for (int i = 0; i < (width); i++) {
                for (int j = 0; j < (height); j++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    int new_x = pos_y;
                    int new_y = w - pos_x - 1;
                    colorBuff[new_y * (h) + new_x] = fill_color(buffer);
                }
            }

            break;
        }

        case LCD_TRANSFORM_ROTATE_90: {
            for (int i = 0; i < (width); i++) {
                for (int j = 0; j < (height); j++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    int new_x = h - pos_y - 1;
                    int new_y = pos_x;
                    colorBuff[new_y * (h) + new_x] = fill_color(buffer);
                }
            }

            break;
        }

        case LCD_TRANSFORM_MIRROR_X: {
            for (int j = 0; j < (height); j++) {
                for (int i = 0; i < (width); i++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    int new_x = pos_x;
                    int new_y = h - pos_y - 1;
                    colorBuff[new_y * (h) + new_x] = fill_color(buffer);
                }
            }

            break;
        }

        default: {
            for (int j = 0; j < (height); j++) {
                for (int i = 0; i < (width); i++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    colorBuff[pos_y * (w) + pos_x] = fill_color(buffer);
                }
            }

            break;
        }

    }

    // mem_unlock();
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP static int lcd_get_sys_icon_height(void)
{
    // return g_lcd_config.sys_icon_height;
    return g_lcd_config.sys_icon_height;
}

OSI_SECTION_RW_KEEP int sys_icon_init(void **paint_icon)
{
    SDK_LOG_V("enter");
    lcd_iconHandle_t *handle;
    uint32_t lcd_w, lcd_h;
    lcd_get_size(&lcd_h, &lcd_w, NULL);
    handle = (lcd_iconHandle_t *)malloc(sizeof(lcd_iconHandle_t));
    handle->icon_x = 0;
    handle->icon_y = 0;
    handle->icon_width = lcd_w;
    handle->icon_height = lcd_get_sys_icon_height();
    handle->icon_back_color = rgb888_to_rgb565(0xdededd);
    handle->iconBuff = alloc_framebuffer_memory(lcd_w, lcd_get_sys_icon_height());

    if (NULL == handle->iconBuff) {
        SDK_LOG_W("malloc iconBuff space fail!");
        return -1;
    }

    handle->back_iconBuff = alloc_framebuffer_memory(lcd_w, lcd_get_sys_icon_height());

    if (NULL == handle->back_iconBuff) {
        SDK_LOG_W("malloc back_iconBuff space fail!");
        return -1;
    }

    handle->mix_iconBuff = alloc_framebuffer_memory(lcd_w, lcd_get_sys_icon_height());

    if (NULL == handle->mix_iconBuff) {
        SDK_LOG_W("malloc mix_iconBuff space fail!");
        return -1;
    }

    *paint_icon = handle;
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int sys_icon_deinit(void *paint_icon)
{
    SDK_LOG_V("enter");

    if (NULL == paint_icon) {
        SDK_LOG_E("no icon need to be destroyed!");
        return -1;
    }

    free_framebuffer_memory((void *)((lcd_iconHandle_t *)paint_icon)->iconBuff);
    free_framebuffer_memory((void *)((lcd_iconHandle_t *)paint_icon)->back_iconBuff);
    free_framebuffer_memory((void *)((lcd_iconHandle_t *)paint_icon)->mix_iconBuff);
    free(paint_icon);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP void set_icon_color(uint32_t back_color, void *paint_icon)
{
    if (paint_icon != NULL) {
        ((lcd_iconHandle_t *)paint_icon)->icon_back_color = (rgb888_to_rgb565(back_color));
    }
}

OSI_SECTION_RW_KEEP static void clear_icon_color(int x, int y, uint32_t width, uint32_t height, void *paint_icon)
{
    SDK_LOG_V("enter");

    if (y >= g_lcd_config.sys_icon_height) {
        return;
    }

    uint32_t h, w;
    lcd_get_size(&h, &w, NULL);

    if (paint_icon == NULL) {
        return;
    }

    lcd_hal_refresh_done();
    uint16_t *back_iconBuff = ((uint16_t *)((lcd_iconHandle_t *)paint_icon)->back_iconBuff);
    uint16_t icon_back_color = ((lcd_iconHandle_t *)paint_icon)->icon_back_color;

    switch (g_lcd_config.sw_screen_dir) {
        case LCD_TRANSFORM_ROTATE_270: {
            for (int i = 0; i < (width); i++) {
                for (int j = 0; j < (height); j++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    int new_x = pos_y;
                    int new_y = w - pos_x - 1;
                    back_iconBuff[new_y *g_lcd_config.sys_icon_height + new_x] = fill_color(icon_back_color);
                }
            }

            break;
        }

        case LCD_TRANSFORM_ROTATE_90: {
            for (int i = 0; i < (width); i++) {
                for (int j = 0; j < (height); j++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    int new_x = g_lcd_config.sys_icon_height - pos_y - 1;
                    int new_y = pos_x;
                    back_iconBuff[new_y *g_lcd_config.sys_icon_height + new_x] = fill_color(icon_back_color);
                }
            }

            break;
        }

        default: {
            for (int j = 0; j < (height); j++) {
                for (int i = 0; i < (width); i++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    back_iconBuff[pos_y *g_lcd_config.sys_icon_height + pos_x] = fill_color(icon_back_color);
                }
            }

            break;
        }

    }

    SDK_LOG_V("leave");
}

OSI_SECTION_RW_KEEP uint16_t lcd_get_icon_status(void)
{
    return g_icon_status;
}

/**
 * @brief  draw sys icon
 * @note
 * @param  x: x
 * @param  y: y
 * @param  width: width
 * @param  height: height
 * @param  buffer: color table
 * @param  layer:  1 iconBuff， 0 back_iconBuff
 * @param  *paint_icon: paint
 * @retval
 */
OSI_SECTION_RW_KEEP static int icon_draw_rect_sys(int x, int y, uint32_t width, uint32_t height, uint16_t buffer,
        uint8_t layer, void *paint_icon)
{
    // SDK_LOG_V("enter");
    // mem_lock();
    lcd_hal_refresh_done();
    uint32_t h, w;
    lcd_get_size(&h, &w, NULL);

    if (paint_icon == NULL) {
        return -1;
    }

    if (y < lcd_get_sys_icon_height()) {
        uint16_t *iconBuff = NULL;

        if (layer == 0) {
            iconBuff = ((uint16_t *)((lcd_iconHandle_t *)paint_icon)->back_iconBuff);
        } else {
            iconBuff = ((uint16_t *)((lcd_iconHandle_t *)paint_icon)->iconBuff);
        }

        switch (lcd_get_direction()) {
            case LCD_TRANSFORM_ROTATE_270: {
                for (int i = 0; i < (width); i++) {
                    for (int j = 0; j < (height); j++) {
                        int pos_x = x + i;
                        int pos_y = y + j;
                        int new_x = pos_y;
                        int new_y = w - pos_x - 1;
                        iconBuff[new_y *lcd_get_sys_icon_height() + new_x] = fill_color(buffer);
                    }
                }

                break;
            }

            case LCD_TRANSFORM_ROTATE_90: {
                for (int i = 0; i < (width); i++) {
                    for (int j = 0; j < (height); j++) {
                        int pos_x = x + i;
                        int pos_y = y + j;
                        int new_x = g_lcd_config.sys_icon_height - pos_y - 1;
                        int new_y = pos_x;
                        iconBuff[new_y *lcd_get_sys_icon_height() + new_x] = fill_color(buffer);
                    }
                }

                break;
            }

            default: {
                for (int j = 0; j < (height); j++) {
                    for (int i = 0; i < (width); i++) {
                        int pos_x = x + i;
                        int pos_y = y + j;
                        iconBuff[pos_y * (lcd_get_sys_icon_height()) + pos_x] = fill_color(buffer);
                    }
                }

                break;
            }
        }
    }

    // mem_unlock();
    // SDK_LOG_V("leave");
    return 0;
}

lcdFlipComplete __lfc = NULL;

OSI_SECTION_RW_KEEP void __lcd_flip(uint8_t sync)
{

    SDK_LOG_V("enter");
    SDK_LOG_V("mem_lock");
    if (false == lcd_get_backlight_status()) {
        return;
    }

    if (NULL == paint_handle) {
        SDK_LOG_E("lcd_flip error, paint handle is NULL!");
        return;
    }
    uint32_t x, y, w, h, area;
    lcd_direction_t dir;
    x = g_lcd_config.paint_area.x;
    y = g_lcd_config.paint_area.y;
    w = g_lcd_config.paint_area.w;
    h = g_lcd_config.paint_area.h;
    area = g_lcd_config.paint_area.area;
    dir = g_lcd_config.hw_screen_dir;
    lcd_hal_config_screen(x, y, w, h, dir);
    SDK_LOG_V("lealcd_hal_refresh_screenve");
    SDK_LOG_V("g_multi_layer %d ", g_multi_layer);
    switch (g_multi_layer) {
        case 0:
            lcd_hal_refresh_screen((void *)((uint8_t *)((lcd_paintHandle_t *)paint_handle)->colorBuff), area, sync);
            break;
        case 1: {
            uint16_t *forecolor = (uint16_t *)((lcd_paintHandle_t *)paint_handle)->colorBuff;
            uint16_t *backcolor = (uint16_t *)((lcd_paintHandle_t *)paint_handle)->back_colorBuff;
            for (int i = 0; i < w *h; i++) {
                backcolor[i] = ((forecolor[i] == 0xffff) ? backcolor[i] : forecolor[i]);
            }
            lcd_hal_refresh_screen((void *)((uint8_t *)((lcd_paintHandle_t *)paint_handle)->back_colorBuff), area, sync);
            break;
        }
        case 2:
            lcd_hal_refresh_screen((void *)((uint8_t *)((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer), area, sync);
            break;
        default:
            break;
    }
    SDK_LOG_V("addr 0x%x  size 0x%x", ((uint8_t *)((lcd_paintHandle_t *)paint_handle)->colorBuff), area);
    SDK_LOG_V("sync flip");
    SDK_LOG_V("leave");

    return;
}

OSI_SECTION_RW_KEEP void lcd_flip(void)
{
    if (false == lcd_get_backlight_status()) {
        return;
    }
    flip_lock();
    __lcd_flip(false);
    flip_unlock();
}

OSI_SECTION_RW_KEEP int lcd_flip_sync(void)
{
    if (false == lcd_get_backlight_status()) {
        return 0;
    }
    flip_lock();
    __lcd_flip(true);
    flip_unlock();
    return 0;
}

OSI_SECTION_RW_KEEP uint8_t lcd_get_multi_layer(void)
{
    return g_multi_layer;
}

OSI_SECTION_RW_KEEP int lcd_set_multi_layer(uint8_t en)
{
    SDK_LOG_V("enter");
    // mem_lock();
    lcd_hal_refresh_done();
    g_multi_layer = en;

    switch (g_multi_layer) {
        case 0:
            if (((lcd_paintHandle_t *)paint_handle)->back_colorBuff) {
                free(((lcd_paintHandle_t *)paint_handle)->back_colorBuff);
                ((lcd_paintHandle_t *)paint_handle)->back_colorBuff = NULL;
            }
            if (((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer) {
                free(((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer);
                ((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer = NULL;
            }
            break;
        case 1:
            if (((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer) {
                free(((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer);
                ((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer = NULL;
            }
            if (NULL == ((lcd_paintHandle_t *)paint_handle)->back_colorBuff) {
                uint32_t w = 0, h = 0;
                lcd_get_size(&h, &w, NULL);
                osiHeapFreeSizeGet(NULL, "lcd_set_multi_layer");
                ((lcd_paintHandle_t *)paint_handle)->back_colorBuff = alloc_framebuffer_memory(w, h);

                if (NULL == ((lcd_paintHandle_t *)paint_handle)->back_colorBuff) {
                    SDK_LOG_W("malloc back_colorBuff fail");
                    // mem_unlock();
                    return -1;
                }
            } else {
                SDK_LOG_V("back_colorBuff are exit");
                // mem_unlock();
                return 0;
            }
            break;
        case 2:
            if (NULL == ((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer) {
                uint32_t w = 0, h = 0;
                lcd_get_size(&h, &w, NULL);
                osiHeapFreeSizeGet(NULL, "lcd_set_multi_layer");
                ((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer = alloc_framebuffer_memory(w, h);

                if (NULL == ((lcd_paintHandle_t *)paint_handle)->sys_msg_colorbuffer) {
                    SDK_LOG_W("malloc sys_msg_colorbuffer fail");
                    // mem_unlock();
                    return -1;
                }
            } else {
                SDK_LOG_V("sys_msg_colorbuffer are exit");
                // mem_unlock();
                return 0;
            }
            break;
        default:
            break;
    }

    SDK_LOG_V("addr 0x%x 0x%x", ((lcd_paintHandle_t *)paint_handle)->colorBuff,
              ((lcd_paintHandle_t *)paint_handle)->back_colorBuff);
    // mem_unlock();
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int lcd_get_size(uint32_t *height, uint32_t *width, uint32_t *bits)
{
    int ret = 0;
    uint32_t w = 0, h = 0;

    lcd_hal_get_size(&h, &w, bits);

    if ((height != NULL) && (width != NULL)) {
        *height = h - g_lcd_config.sys_icon_height;
        *width = w;

        if ((g_lcd_config.sw_screen_dir == LCD_TRANSFORM_ROTATE_90)
            || (g_lcd_config.sw_screen_dir == LCD_TRANSFORM_ROTATE_270)) {
            *height = w - g_lcd_config.sys_icon_height;
            *width = h;
        }
        SDK_LOG_V("(%d, %d)", *width, *height);
    } else {
        ret = -1;
    }

    if (bits == NULL) {
        ret = -1;
    }

    return ret;
}

OSI_SECTION_RW_KEEP int lcd_get_status_bar_height(void)
{
    return g_lcd_config.sys_icon_height;
}

OSI_SECTION_RW_KEEP void lcd_check_sys_font(void)
{
    struct stat fileStat;

    if (stat(USED_FONT, &fileStat) < 0) {
        int fd = open(USED_FONT, O_RDONLY);
        /*file system has been erased*/
        SDK_LOG_W("font '%s' is null! use default ascii font!", USED_FONT);
        font_close_user(font_used_handle);
        g_sys_ascii_font_handle = (lcd_fontHandle_t *)malloc(sizeof(lcd_fontHandle_t));
        g_sys_ascii_font_handle->fd = 0;
        g_sys_ascii_font_handle->second_fd = 0;
        g_sys_ascii_font_handle->font_charset = NULL;
        g_sys_ascii_font_handle->font_width = get_sys_ascii_size() / 2;
        g_sys_ascii_font_handle->font_height = get_sys_ascii_size();
        g_sys_ascii_font_handle->font_size = get_sys_ascii_size();
        font_used_handle = (void *)g_sys_ascii_font_handle;
        fs_close(fd);
    }
}

OSI_SECTION_RW_KEEP void *lcd_get_sys_font(void)
{
    return font_used_handle;
}

OSI_SECTION_RW_KEEP void *lcd_get_sys_paint(void)
{
    SDK_LOG_V("paint_handle 0x%x", paint_handle);
    return paint_handle;
}

/*buffer*/
OSI_SECTION_RW_KEEP void *alloc_framebuffer_memory(int width, int height)
{
    return alloc_framebuffer(width, height);
}

OSI_SECTION_RW_KEEP int free_framebuffer_memory(void *handle)
{
    int res = 0;
    res = free_framebuffer(handle);
    return res;
}

OSI_SECTION_RW_KEEP const uint8_t *lcd_get_buffer_addr(void)
{
    uint32_t h = 0, w = 0;
    lcd_get_size(&h, &w, NULL);
    if (g_lcd_buffer_addr == NULL) {
        g_lcd_buffer_addr = alloc_framebuffer(w, h); // 565 (2byte, uint16)
    }
    uint16_t *colorBuff = (uint16_t *)((lcd_paintHandle_t *)paint_handle)->colorBuff;

    switch (g_lcd_config.sw_screen_dir) {
        case LCD_TRANSFORM_ROTATE_270: {
            for (int j = 0; j < (h); j++) {
                for (int i = 0; i < (w); i++) {
                    int pos_x = i;
                    int pos_y = j;
                    int new_x = pos_y;
                    int new_y = w - pos_x - 1;
                    ((uint16_t *)g_lcd_buffer_addr)[pos_y *w + pos_x] =
                        (colorBuff[new_y * (h) + new_x]);
                }
            }
            break;
        }

        case LCD_TRANSFORM_ROTATE_90: {
            for (int j = 0; j < (h); j++) {
                for (int i = 0; i < (w); i++) {
                    int pos_x = i;
                    int pos_y = j;
                    int new_x = h - pos_y - 1;
                    int new_y = pos_x;
                    ((uint16_t *)g_lcd_buffer_addr)[pos_y *w + pos_x] =
                        (colorBuff[new_y * (h) + new_x]);
                }
            }

            break;
        }

        case LCD_TRANSFORM_MIRROR_X: {
            for (int j = 0; j < (h); j++) {
                for (int i = 0; i < (w); i++) {
                    int pos_x = i;
                    int pos_y = h - j - 1;
                    ((uint16_t *)g_lcd_buffer_addr)[pos_y *w + pos_x] =
                        (colorBuff[pos_y * (h) + pos_x]);
                }
            }

            break;
        }

        default: { /*normal*/
            for (int j = 0; j < (h); j++) {
                for (int i = 0; i < (w); i++) {
                    int pos_x = i;
                    int pos_y = j;
                    ((uint16_t *)g_lcd_buffer_addr)[pos_y *w + pos_x] =
                        (colorBuff[pos_y * (h) + pos_x]);
                }
            }
        }
        break;
    }

    return (uint8_t *)g_lcd_buffer_addr;
}

OSI_SECTION_RW_KEEP int lcd_free_buffer_addr(void)
{
    int res = 0;
    res = free_framebuffer(g_lcd_buffer_addr);
    if (res == 0) {
        g_lcd_buffer_addr = NULL;
    }
    return res;
}

OSI_SECTION_RW_KEEP static int lcd_set_direction(void)
{
    uint32_t id = 0;
    id = lcd_hal_get_id();

    uint32_t realHeight, realWidth;
    lcd_hal_get_size(&realHeight, &realWidth, NULL);

    switch (id) {
        case 0x077353:
            g_lcd_config.id = id;
            g_lcd_config.sw_screen_dir = LCD_TRANSFORM_ROTATE_90;
            g_lcd_config.hw_screen_dir = LCD_DIR_ROTATE_180;
            g_lcd_config.sys_icon_height = 20;
            g_lcd_config.sys_icon_area.x = realWidth - g_lcd_config.sys_icon_height;
            g_lcd_config.sys_icon_area.y = 0;
            g_lcd_config.sys_icon_area.w = g_lcd_config.sys_icon_height;
            g_lcd_config.sys_icon_area.h = realHeight;
            g_lcd_config.sys_icon_area.area = g_lcd_config.sys_icon_area.w *g_lcd_config.sys_icon_area.h * 1;
            g_lcd_config.paint_area.x = 0;
            g_lcd_config.paint_area.y = 0;
            g_lcd_config.paint_area.w = realWidth - g_lcd_config.sys_icon_height;
            g_lcd_config.paint_area.h = realHeight;
            g_lcd_config.paint_area.area = g_lcd_config.paint_area.w *g_lcd_config.paint_area.h * 1;
            break;

        case 0x773532:
            g_lcd_config.id = id;
            g_lcd_config.sw_screen_dir = LCD_TRANSFORM_ROTATE_90;
            g_lcd_config.hw_screen_dir = LCD_DIR_NORMAL;
            g_lcd_config.sys_icon_height = 30;
            g_lcd_config.sys_icon_area.x = realWidth - g_lcd_config.sys_icon_height;
            g_lcd_config.sys_icon_area.y = 0;
            g_lcd_config.sys_icon_area.w = g_lcd_config.sys_icon_height;
            g_lcd_config.sys_icon_area.h = realHeight;
            g_lcd_config.sys_icon_area.area = g_lcd_config.sys_icon_area.w *g_lcd_config.sys_icon_area.h * 1;
            g_lcd_config.paint_area.x = 0;
            g_lcd_config.paint_area.y = 0;
            g_lcd_config.paint_area.w = realWidth - g_lcd_config.sys_icon_height;
            g_lcd_config.paint_area.h = realHeight;
            g_lcd_config.paint_area.area = g_lcd_config.paint_area.w *g_lcd_config.paint_area.h * 1;
            break;

        case 0x307789:
            g_lcd_config.id = id;
            g_lcd_config.sw_screen_dir = LCD_TRANSFORM_NONE;
            g_lcd_config.hw_screen_dir = LCD_DIR_NORMAL;
            g_lcd_config.sys_icon_height = 30;
            g_lcd_config.sys_icon_area.x = 0;
            g_lcd_config.sys_icon_area.y = 0;
            g_lcd_config.sys_icon_area.w = g_lcd_config.sys_icon_height;
            g_lcd_config.sys_icon_area.h = realHeight;
            g_lcd_config.sys_icon_area.area = g_lcd_config.sys_icon_area.w *g_lcd_config.sys_icon_area.h * 2;
            g_lcd_config.paint_area.x = g_lcd_config.sys_icon_height;
            g_lcd_config.paint_area.y = 0;
            g_lcd_config.paint_area.w = realWidth - g_lcd_config.sys_icon_height;
            g_lcd_config.paint_area.h = realHeight;
            g_lcd_config.paint_area.area = g_lcd_config.paint_area.w *g_lcd_config.paint_area.h * 2;
            break;

        case 0x007567:
        default:
            g_lcd_config.id = id;
            g_lcd_config.sw_screen_dir = LCD_TRANSFORM_NONE;
            g_lcd_config.hw_screen_dir = LCD_DIR_NORMAL;
            g_lcd_config.sys_icon_height = 0;
            g_lcd_config.sys_icon_area.x = 0;
            g_lcd_config.sys_icon_area.y = 0;
            g_lcd_config.sys_icon_area.w = 0;
            g_lcd_config.sys_icon_area.h = 0;
            g_lcd_config.sys_icon_area.area = 0;
            g_lcd_config.paint_area.x = 0;
            g_lcd_config.paint_area.y = 0;
            g_lcd_config.paint_area.w = realWidth;
            g_lcd_config.paint_area.h = realHeight;
            g_lcd_config.paint_area.area = g_lcd_config.paint_area.w *g_lcd_config.paint_area.h * 2;
            break;
    }

    return 0;
}

OSI_SECTION_RW_KEEP int lcd_get_direction(void)
{
    return g_lcd_config.sw_screen_dir;
}

static void flip_async_thread(void *param)
{
    uint8_t flip_msg = 0;

    while (1) {
        if (true == osiMessageQueueTryGet(msgQ_async_flip, (void *)&flip_msg, 0xffffffff)) {
            if (false == lcd_get_backlight_status()) {
                return;
            }
            //flip
            SDK_LOG_V("------async flip enter");
            flip_lock();
            __lcd_flip(true);
            lcd_cam_preview = false;
            SDK_LOG_V("-----mem_unlock");
            flip_unlock();
            SDK_LOG_V("-----async flip leave");
        }
    }
}

OSI_SECTION_RW_KEEP int lcd_init()
{
    SDK_LOG_V("enter");

    if (false == lcd_hal_get_init_status()) {
        if (lcd_hal_init() == LCD_FAIL) {
            return LCD_FAIL;
        }
    }
    /*********** ASYNC FLIP ****** */
    msgQ_async_flip = osiMessageQueueCreate(1, 1);
    sema_mem_lock = osiSemaphoreCreate(1, 1);
    sema_flip_lock = osiSemaphoreCreate(1, 1);

    // lcd_set_sys_icon_height();
    /*set direction*/
    lcd_set_direction();
    /*size*/
    uint32_t realHeight, realWidth;
    lcd_get_size(&realHeight, &realWidth, NULL);
    SDK_LOG_D("paintWidth %d paintHeight %d", realWidth, realHeight);
    USED_FONT = FONT_CH;


    /*init paint user*/
    int res = paint_create_user(realWidth, realHeight, get_sys_ascii_size(), &paint_handle);

    if (-1 == res) {
        SDK_LOG_E("paint create failed: %d", res);
        paint_handle = NULL;
    } else {
        SDK_LOG_D("paint create success: %d", res);
    }

    res = font_open_user(USED_FONT, FONT_CHARSET_ASCII, 0, get_sys_ascii_size(), &font_ch_handle);

    if (-1 == res) {
        SDK_LOG_E("    '%s' font  open failed: %d",  USED_FONT, res);
    } else {
        SDK_LOG_D("    '%s' font  open success: %d", USED_FONT, res);
    }

    font_used_handle = font_ch_handle ;

    osiThreadCreate("flip_async", flip_async_thread, NULL, 120, 1024);

    SDK_LOG_V("leave");

    return LCD_OK;
}

OSI_SECTION_RW_KEEP int get_sys_ascii_size(void)
{
    uint32_t id = 0;
    int rev = 0;
    id = lcd_hal_get_id();

    switch (id) {
        case LCD_MOUDLE_ID_ST7735S32:
        case LCD_MOUDLE_ID_ST7789_3W:
            rev = 24;
            break;

        case LCD_MOUDLE_ID_ST7567A:
            rev = 12;
            break;

        case LCD_MOUDLE_ID_ST7735S3:
            rev = 16;
            break;
        case LCD_MOUDLE_ID_JD9853:
            rev = 24;
            break;

        default:
            rev = 24;
            break;
    }

    return rev;
}

OSI_SECTION_RW_KEEP int get_select_ext_flash(uint32_t flashSize)
{
    int rev = 0;

    switch (flashSize) {
        case 2:
            rev = 2;
            break;

        case 4:
            rev = 1;
            break;

        case 8:
        default:
            rev = 2;
            break;
    }

    return rev;
}

OSI_SECTION_RW_KEEP int support_icon(void)
{
    int rev = 0;
    return rev;
}

/*backlight*/
uint16_t lcd_get_backlight_time(void)
{
    return lcd_backlight_time;
}

OSI_SECTION_RW_KEEP int lcd_set_backlight(uint8_t en)
{
    SDK_LOG_V("en %d", en);
    cust_lcd_hal_set_backlight(en);
    return 0;
}

OSI_SECTION_RW_KEEP int lcd_get_backlight_status(void)
{
    return cust_lcd_hal_get_backlight_status();
}

OSI_SECTION_RW_KEEP int lcd_set_backlight_time(int second)
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
        SDK_LOG_D("time is out off range(5~2000)!");
        return -1;
    }

    // changge lcd backlight time value
    lcd_backlight_time = second;

    //set timeout
    if (NULL == lcd_timer) {
        // lcd_hal_set_backlight(true);
        lcd_set_backlight(true);
        lcd_timer = osiTimerCreate(callcack_backlight_timeout, NULL);
        osiTimerStart(lcd_timer, second * 1000);
        SDK_LOG_D("timeout is %dS!", second);
    } else {
        (void)osiTimerStop(lcd_timer);
        osiTimerStart(lcd_timer, second * 1000);
        SDK_LOG_D("timer is running!");
    }

    return 0;
}

OSI_SECTION_RW_KEEP static void callcack_backlight_timeout(void *ctx)
{
    (void)ctx;
    // lcd_hal_set_backlight(false);
    lcd_set_backlight(false);
    (void)osiTimerStop(lcd_timer);
    osiTimerDelete(lcd_timer);
    lcd_timer = NULL;
    SDK_LOG_I("timeout! Turn off the backlight!");
    return ;
}


void lcd_hal_set_brightness(uint8_t val)
{
    prv_brightness = val & 0x7f;
    drvIsinkPwmEnable(prv_brightness_en, 3, prv_brightness);
}
void lcd_hal_enable_brightness(bool en)
{
    prv_brightness_en = en;
    drvIsinkPwmEnable(en, 3, prv_brightness);
}

uint8_t lcd_hal_get_brightness(void)
{
    return prv_brightness;
}



OSI_SECTION_RW_KEEP static void lcd_hal_ReadID(void)
{
    lcd_config.lcd_id = LCD_MOUDLE_ID_JD9853;
// #ifdef CONFIG_MOD_LCD_SELECT_RGB_LCD_ST7796
//     lcd_config.lcd_id = LCD_MOUDLE_ID_ST7796;
// #elif defined CONFIG_MOD_LCD_SELECT_RGB_LCD_NV3041A
//     lcd_config.lcd_id = LCD_MOUDLE_ID_NV3041A;
// #elif defined CONFIG_MOD_LCD_SELECT_RGB_LCD_AXS15231
//     lcd_config.lcd_id = LCD_MOUDLE_ID_AXS15231;
// #elif defined CONFIG_MOD_LCD_SELECT_RGB_LCD_AXS15261
//     lcd_config.lcd_id = LCD_MOUDLE_ID_AXS15261;
// #else
//     lcd_config.lcd_id = LCD_MOUDLE_ID_UNKNOWN;
// #endif
}

OSI_SECTION_RW_KEEP int lcd_hal_init(void)
{
    SDK_LOG_V("enter");

    if (true != lcd_config.init_status) {
        lcd_hal_ReadID();

        if (LCD_MOUDLE_ID_UNKNOWN == lcd_config.lcd_id) {
            SDK_LOG_E("lcd id is 0x%06x", lcd_config.lcd_id);
            return -1;
        } else {
            SDK_LOG_I("lcd id is 0x%06x", lcd_config.lcd_id);
        }

#ifndef CONFIG_MOD_LCD_SELECT_RGB_LCD_AXS15261
        //开机logo 不需要再次初始化
        drvLcdInit();
#endif

        lcd_config.lcd_dev = drvLcdOpen(lcd_config.lcd_id, NULL);

        if (lcd_config.lcd_dev == NULL) {
            SDK_LOG_E("lcd init fail!");
            return -1;
        } else {
            SDK_LOG_I("drvLcdOpen 0x%x", lcd_config.lcd_dev);
        }

        lcd_config.dev_desc = (drvLcdPanelDesc_t *)drvLcdGetDesc(lcd_config.lcd_dev);
        lcd_config.backlight_status = true;
        lcd_config.init_status = true;
        SDK_LOG_D("lcd_config.dev_desc->width-%d lcd_config.dev_desc->height-%d", lcd_config.dev_desc->width,
                  lcd_config.dev_desc->height);
        SDK_LOG_V("lcd_dev 0x%x ", lcd_config.lcd_dev);

        lcd_hal_set_brightness(0x55);
        lcd_hal_enable_brightness(true);
#ifdef CONFIG_MOD_LVGL
        uint8_t bright_config[1];
        int read_success = sys_get_internal_config(LVGL_SYSCFG_BACKLIGHT, bright_config, sizeof(uint8_t));
        if (read_success != 0) {
            lcd_hal_set_brightness(0x55);
        }
#endif

    } else {
        SDK_LOG_W("lcd has been initialized!");
    }

    SDK_LOG_V("leave");
    return lcd_config.lcd_id;
}

OSI_SECTION_RW_KEEP uint32_t lcd_hal_get_id(void)
{
    return lcd_config.lcd_id;
}

OSI_SECTION_RW_KEEP bool lcd_hal_get_init_status(void)
{
    return lcd_config.init_status;
}

OSI_SECTION_RW_KEEP void lcd_hal_get_size(uint32_t *height, uint32_t *width, uint32_t *bits)
{
    lcd_config.dev_desc = (drvLcdPanelDesc_t *)drvLcdGetDesc(lcd_config.lcd_dev);

    if (height != NULL) {
        *height = lcd_config.dev_desc->height;
    }

    if (width != NULL) {
        *width = lcd_config.dev_desc->width;
    }

    if (bits != NULL) {
        switch (lcd_config.dev_desc->out_fmt) {
            case DRV_LCD_OUT_FMT_8BIT_RGB332:
            case DRV_LCD_OUT_FMT_8BIT_RGB444:
            case DRV_LCD_OUT_FMT_8BIT_RGB565:
                *bits = 8;
                break;

            case DRV_LCD_OUT_FMT_16BIT_RGB332:
            case DRV_LCD_OUT_FMT_16BIT_RGB444:
            case DRV_LCD_OUT_FMT_16BIT_RGB565:
                *bits = 16;
                break;

            default:
                *bits = 1;
                break;
        }
    }
}

OSI_SECTION_RW_KEEP void lcd_hal_config_screen(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_direction_t dir)
{
    SDK_LOG_V("enter");
    lcd_config.areas.x = x;
    lcd_config.areas.y = y;
    lcd_config.areas.w = w;
    lcd_config.areas.h = h;
    lcd_config.dev_desc->ops.blit_prepare(lcd_config.lcd_dev, (drvLcdDirection_t)dir, &lcd_config.areas);
    // SDK_LOG_I("%d %d %d %d", x, y, w, h);
    SDK_LOG_V("leave");
}

OSI_SECTION_RW_KEEP void lcd_hal_refresh_screen(void *data_buffer, uint32_t data_size, uint8_t sync)
{
    SDK_LOG_V("enter");
    drvLcdBlockTransfer(lcd_config.lcd_dev, data_buffer, data_size, sync, NULL);
    SDK_LOG_V("leave");
}

OSI_SECTION_RW_KEEP void lcd_hal_refresh_screen_withCb(void *data_buffer, uint32_t data_size, uint8_t sync,
        drvLcdTransferDoneCb funcCb)
{
    SDK_LOG_V("enter");
    drvLcdBlockTransfer(lcd_config.lcd_dev, data_buffer, data_size, sync, funcCb);
    SDK_LOG_V("leave");
}

OSI_SECTION_RW_KEEP void lcd_hal_refresh_done(void)
{
    drvLcdWaitTransferDone(lcd_config.lcd_dev);
}

OSI_SECTION_RW_KEEP void *alloc_framebuffer(int width, int height)
{
    void *addr;
    addr = (uint16_t *)malloc(width *height * 2 / 8 * 9);

    // addr = (uint16_t *)malloc(width * height * 2);
    if (addr == NULL) {
        SDK_LOG_W("malloc space fail!");
        return NULL;
    }

    memset(addr, 0xff, width *height * 2 / 8 * 9);
    // memset(addr, 0xff, width * height *2);

    SDK_LOG_V("addr:0x%x width:%d, height:%d", addr, width, height);
    return addr;
}

OSI_SECTION_RW_KEEP int free_framebuffer(void *handle)
{
    if (handle == NULL) {
        SDK_LOG_W("free space fail!");
        return -1;
    }

    free(handle);
    return 0;
}

OSI_SECTION_RW_KEEP void cust_lcd_hal_set_backlight(bool enable)
{
    SDK_LOG_V("enter %d", enable);

    if (enable) {
        osiSetPMEnableSleep(false);
        lcd_config.backlight_status = true;
#ifndef CONFIG_MOD_LVGL_V8
#ifdef CONFIG_MOD_LCD_TYPE_RGB_LCD
        if (lcd_config.backlight_status == false) {
            drvLcdInit();
            drvLcdOpen(lcd_config.lcd_id, NULL);

            extern void lcd_flip_icon(void);
            lcd_flip_icon();
            extern void lcd_flip_sync(void);
            lcd_flip_sync();
        }
#endif
#endif
    } else {
        osiSetPMEnableSleep(true); // open sleep when off backlight
        lcd_config.backlight_status = false;
    }

    drvLcdSetBackLightEnable(lcd_config.lcd_dev, enable);

    SDK_LOG_V("leave");
}

OSI_SECTION_RW_KEEP int cust_lcd_hal_get_backlight_status(void)
{
    return lcd_config.backlight_status;
}

int framework_lcd9853_gpio_init(unsigned int SCL_PIN_MFP, unsigned int SCL_PIN_NUM, unsigned int SDA_PIN_MFP, unsigned int SDA_PIN_NUM){
    const unsigned int mfp_cfg_ext_touch[] = {
        MFP_REG(SCL_PIN_MFP) | MFP_AF0 | MFP_DRIVE_MEDIUM | MFP_PULL_NONE | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    mfp_config(mfp_cfg_ext_touch);
    GpioSetDirection(SCL_PIN_NUM, GPIO_OUT_PIN);
    GpioSetLevel(SCL_PIN_NUM, 0);

    unsigned int mfp_spk[] = {
        // codec en
        MFP_REG(SDA_PIN_MFP) | MFP_AF0 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,

        MFP_EOC
    };
    drvGpioPinMuxConfig(mfp_spk);
    GpioSetDirection(SDA_PIN_NUM, GPIO_OUT_PIN);
    GpioSetLevel(SDA_PIN_NUM, 1);
    return 0;
}

void app_lcd_backlight_off(osiTimer_t* backlight_timer)
{
    cust_lcd_hal_set_backlight(false);
    if(backlight_timer != NULL){
        osiTimerStop(backlight_timer);
    }
}
void app_lcd_backlight_on(void)
{
    cust_lcd_hal_set_backlight(true);
}

void lcd_start_work(void *ctx,osiTimer_t* backlight_timer,lcd_handle_t *g_lcd){
    app_lcd_backlight_on();
    if(backlight_timer != NULL){
        osiTimerStop(backlight_timer);
        osiTimerDelete(backlight_timer);
    }
    backlight_timer = osiTimerCreate(app_lcd_backlight_off, NULL);
    if(backlight_timer == NULL){
        return -1;
    }
    osiTimerStart(backlight_timer, g_lcd->backlight_timeout_ms);
}

int app_lcd_backlight_timer_start(int timeout_ms,lcd_handle_t* g_lcd)
{
    if(g_lcd == NULL){
        return -1;
    }
    g_lcd->backlight_timeout_ms = timeout_ms;
    osiWorkEnqueue(g_lcd->work, osiSysWorkQueueHighPriority());
    return 0;
}

int app_lcd_clear(int x,int y,int width,int heigth,lcd_handle_t *g_lcd) {
    if (!g_lcd) return -1;
    paint_set_color_user(0xffffff, 0x000000, g_lcd->paint_handle); // White background
    int clear_state=paint_clean_rect_user(x, y, width,heigth, g_lcd->paint_handle);
    return clear_state;
}

int Display_string_lcd(font_t* font_handler,lcd_handle_t* g_lcd,int x,int y,int width,int heigth,int font_size,char *string)
{
    if(string == NULL){
        return -1;
    }
    paint_clean_rect_user(x, y,width, heigth, g_lcd->paint_handle);
    paint_draw_text_user(font_handler, x, y, string, g_lcd->paint_handle);
    return 0;
}



/* Default BMP paths (override via framework_get_bmp_paths in customer code) */
static const char *const s_bmp_paths_default[] = {
    "D:/home/default.bmp",
};

__attribute__((weak)) const char *const *framework_get_bmp_paths(int *count)
{
    if (count) *count = (int)(sizeof(s_bmp_paths_default) / sizeof(s_bmp_paths_default[0]));
    return s_bmp_paths_default;
}



int Display_picture(lcd_handle_t* g_lcd,int start_x,int start_y,int serial)
{
    if (!g_lcd || !g_lcd->paint_handle)
        return -1;
    int bmp_count = 0;
    const char *const *bmp_paths = framework_get_bmp_paths(&bmp_count);
    if (!bmp_paths || bmp_count <= 0)
        return -1;
    if (serial < 0 || serial >= bmp_count)
        serial = 0;

    headerHandle_t *bmp = NULL;
    if (paint_open_picture(bmp_paths[serial], (void **)&bmp) != 0)
        return -1;

    const uint32_t width  = bmp->infoHeader->biWidth;
    const uint32_t height = bmp->infoHeader->biHeight;
    if (bmp->infoHeader->biBitCount != 24 || width > 240) {
        paint_close_picture(bmp);
        return -1;
    }
    const uint32_t row_bytes = ((width * 24 + 31u) >> 5u) << 2u;
    const uint32_t data_base = bmp->fileHeader->bfOffBits;
    paint_set_color_user(0xffffff, 0x000000, g_lcd->paint_handle); // White background
    for (uint32_t y = 0; y < height; y += BMP_CHUNK_LINES) {
        uint32_t lines      = MIN(BMP_CHUNK_LINES, height - y);
        uint32_t start_line = height - (y + lines);   // Bottom line number of the block
        fs_seek(bmp->fd_bmp, data_base + start_line * row_bytes, SEEK_SET);

        for (uint32_t line = 0; line < lines; ++line) {
            uint8_t raw[row_bytes];
            if (fs_read(raw, 1, row_bytes, bmp->fd_bmp) != row_bytes) {
                paint_close_picture(bmp);
                return -1;
            }
            /* Write lines in reverse order to buffer so they won't be flipped when drawn to screen */
            uint16_t *dst = &s_line_buf[(lines - 1 - line) * width];
            for (uint32_t px = 0; px < width; ++px) {
                uint32_t idx = px * 3;
                uint8_t b = raw[idx];
                uint8_t g = raw[idx + 1];
                uint8_t r = raw[idx + 2];
                dst[px] = (uint16_t)(((b & 0xF8) << 8) |
                                     ((g & 0xFC) << 3) |
                                     (r >> 3));
            }
        }
        paint_draw_rect_with_cbuffer(start_x, start_y+y, width, lines, s_line_buf, g_lcd->paint_handle);
    }
    
    paint_close_picture(bmp);
    // lcd_flip_sync();
    return 0;
}

/* -------- Default customer init (weak) -------- */
static int customer_lcd_init_default(void)
{
    /* Minimal safe default: only GPIO mux + backlight on; no dynamic allocations. */
    framework_lcd9853_gpio_init(GPIO_53, 53, GPIO_68, 68);
    drvIsinkPwmEnable(true, 3, 0x28); // backlight
    osiThreadSleep(100);
    lcd_init();
    // g_lcd->paint_handle = lcd_get_sys_paint();
    // g_lcd->font_used_handle = lcd_get_sys_font();
    // lcd_get_size(&g_lcd->w, &g_lcd->h, &g_lcd->bits);
    return 0;
}

int customer_lcd_init(void) __attribute__((weak, alias("customer_lcd_init_default")));