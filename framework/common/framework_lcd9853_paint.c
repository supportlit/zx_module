#include "framework_lcd9853_paint.h"
#include "framework_lcd9853.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs_api.h"
#include "osi_api.h"
#include "common_api.h"
#include <fcntl.h>
#ifdef CONFIG_SYSTEM_FUNC_C_WRAP
#include "c_wrap.h"
#endif
// #include "c_wrap.h"
#include <unistd.h>
//paint.c
#include <string.h>
#include "drv_def.h"

#define BIT_COUNT_1 1
#define BIT_COUNT_4 4
#define BIT_COUNT_8 8
#define BIT_COUNT_24 24

typedef struct _bmpRect {
    int m_x;/**< 矩形起点X坐标 */
    int m_y;/**< 矩形起点Y坐标  */
    uint32_t width;
    uint32_t height;
} bmpRect;

//paint screen
typedef struct {
    uint16_t width;
    uint16_t height;
} paintRange_t;

typedef struct {
    uint16_t font_width;
    uint16_t font_height;
    uint16_t font_size;
    char *font_charset;
    int fd;
    int second_fd;
} fontHandle_t;

typedef struct {
    int index;
    char *charset;
} fontTable_t;

font_t g_fontHandle;

OSI_SECTION_RW_KEEP int font_open_user(const char *fontname, const char *charset, int style, int size, font_t *ppfont)
{
    fontHandle_t *handle;
    char chr;
    const char *name = fontname;
    uint8_t numCnt = 0;
    uint16_t tempChrInHex = 0;
    int tempReturn = 0;

    (void)style;
    handle = (fontHandle_t *)malloc(sizeof(fontHandle_t));

    if (NULL == handle) {
        SDK_LOG_V("malloc fontHandle_t space fail!");
        *ppfont = NULL;
        return -1;
    }

    while (*fontname != '\0') {
        chr = *fontname;

        if ((chr >= '0') && (chr <= '9')) {
            tempChrInHex *= 10;
            tempChrInHex += (chr - '0') ;
            // SDK_LOG_V("chr-%d, tempChrInHex-0x%x numCnt-%d", chr, tempChrInHex, numCnt);
            numCnt ++;
        }

        fontname++;
    }

    handle->font_width = tempChrInHex / 100;
    handle->font_height = tempChrInHex % 100;
    SDK_LOG_V("%d, %d, %d", tempChrInHex, handle->font_width, handle->font_height);
    handle->font_width = size;
    handle->font_height = size;
    SDK_LOG_V("%d, %d, %d", tempChrInHex, handle->font_width, handle->font_height);
    handle->font_charset = (char *)malloc(strlen(charset) + 1);

    if (NULL == handle->font_charset) {
        SDK_LOG_V("malloc charset space fail!");
        free(handle);
        *ppfont = NULL;
        return -1;
    }

    memcpy(handle->font_charset, charset, strlen(charset) + 1);
    SDK_LOG_D("%s, %s, %d, %d", charset, handle->font_charset, size, handle->font_height);
    handle->fd = fs_open(name, "rb");
    // handle->fd = open(name, O_RDONLY);
    SDK_LOG_D("fontname: '%s', fd:%d, size 0x%lx", name, handle->fd, fs_size(name));

    if (handle->fd <= 0) {
        SDK_LOG_V("Font file '%s' not found", name);
        tempReturn = -1;
    }

    handle->font_size = size;
    *ppfont = handle;
    g_fontHandle = handle;
    SDK_LOG_V("g_fontHandle 0x%x", g_fontHandle);
    return tempReturn;
}

OSI_SECTION_RW_KEEP int font_close_user(const font_t pfont)
{
    SDK_LOG_V("enter");
    int tempReturn = 0;

    if (NULL == pfont) {
        SDK_LOG_V("no font pointer to close!");
        return -1;
    }

    if (((fontHandle_t *)pfont)->fd != 0) {
        tempReturn = fs_close(((fontHandle_t *)pfont)->fd);

        if (-1 == tempReturn) {
            SDK_LOG_V("fs_close fail!");
        }
    } else {
        SDK_LOG_V("file don`t exist!");
    }

    if (NULL == ((fontHandle_t *)pfont)->font_charset) {
        SDK_LOG_V("font_charset is NULL!");
    } else {
        free(((fontHandle_t *)pfont)->font_charset);
        ((fontHandle_t *)pfont)->font_charset = NULL;
    }

    free(pfont);
    SDK_LOG_V("leave");
    return tempReturn;
}

OSI_SECTION_RW_KEEP int font_get_word_info_user(const font_t pfont, const char *lpText, int *width, int *height)
{
    if (NULL == lpText) {
        SDK_LOG_W("text is null!");
        return -1;
    }

    int size;

    if (NULL == pfont) {
        SDK_LOG_W("font handle is null!");
        return -1;
    } else {
        size = ((fontHandle_t *)pfont)->font_size;
    }

    if (*lpText < 0x80) {
        *width = size / 2;
    } else {
        *width = size;
    }

    *height = size;
    SDK_LOG_V("width %d height %d", *width, *height);
    return 0;
}

OSI_SECTION_RW_KEEP int font_get_width_user(const font_t pfont, const char *lpText, int len)
{
    int length, pixel_length, size;

    if (NULL == lpText) {
        SDK_LOG_W("text is null!");
        return -1;
    }

    if (len <= 0) {
        SDK_LOG_E("invalid len.");
        return -1;
    }

    if (NULL == pfont) {
        SDK_LOG_W("font handle is null!");
        return -1;
    } else {
        size = ((fontHandle_t *)pfont)->font_size;
    }

    length = (strlen(lpText) > len) ? len : strlen(lpText);
    pixel_length = size / 2 * length;
    // SDK_LOG_V("strlen %d len %d length %d pixel_length %d", strlen(lpText), len, length, pixel_length);

    return pixel_length;
}

OSI_SECTION_RW_KEEP void *get_open_font(void)
{
    if (g_fontHandle == NULL) {
        return NULL;
    }

    return g_fontHandle;
}

//fs_bmp.c

OSI_SECTION_RW_KEEP int open_bitmap(const char *name, void **header)
{
    SDK_LOG_V("enter");
    size_t readLen;
    headerHandle_t *headerHandle;
    //header
    headerHandle = (headerHandle_t *)malloc(sizeof(headerHandle_t));

    if (NULL == headerHandle) {
        SDK_LOG_V("malloc buf is null");
        return -1;
    }

    memset(headerHandle, 0, sizeof(headerHandle_t));
    //file header
    headerHandle->fileHeader = (BITMAPFILEHEADER_ *)malloc(sizeof(BITMAPFILEHEADER_));

    if (NULL == headerHandle->fileHeader) {
        SDK_LOG_W("malloc buf is null");
        return -1;
    }

    memset(headerHandle->fileHeader, 0, sizeof(BITMAPFILEHEADER_));
    //infoheader
    headerHandle->infoHeader = (BITMAPINFOHEADER_ *)malloc(sizeof(BITMAPINFOHEADER_));

    if (NULL == headerHandle->infoHeader) {
        SDK_LOG_W("malloc buf is null");
        return -1;
    }

    memset(headerHandle->infoHeader, 0, sizeof(BITMAPINFOHEADER_));
    headerHandle->fd_bmp =  fs_open(name, "rb+");
    // headerHandle->fd_bmp = open(name, O_RDONLY);

    if (headerHandle->fd_bmp <= 0) {
        SDK_LOG_W("open '%s' failed", name);
        return -1;
    } else {
        SDK_LOG_V("open '%s' success", name);
    }

    readLen = fs_read(headerHandle->fileHeader, 1, sizeof(BITMAPFILEHEADER_), headerHandle->fd_bmp);

    if (sizeof(BITMAPFILEHEADER_) != readLen) {
        SDK_LOG_V("readLen %x sizeof(BITMAPFILEHEADER_) %x ", readLen, sizeof(BITMAPFILEHEADER_));
        return -1;
    }

    if (headerHandle->fileHeader->bfType != 0x4d42) { //BM
        SDK_LOG_V("assert func:%s, line:%d, format:%s.The image isn`t bmp format. ", __func__, __LINE__,
                  &headerHandle->fileHeader->bfType);
        return -1;
    }

    readLen = fs_read(headerHandle->infoHeader, 1, sizeof(BITMAPINFOHEADER_), headerHandle->fd_bmp);

    if (sizeof(BITMAPINFOHEADER_) != readLen) {
        SDK_LOG_V("assert func:%s, line:%d. ", __func__, __LINE__);
        return -1;
    }

    *header = headerHandle;
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int close_bitmap(void *header)
{
    SDK_LOG_V("enter");

    if (NULL == header) {
        SDK_LOG_V("assert func:%s, line:%d. ", __func__, __LINE__);
        SDK_LOG_V("no font pointer to close!");
        return -1;
    }

    if (NULL != (void *)(((headerHandle_t *)header)->fd_bmp)) {
        fs_close(((headerHandle_t *)header)->fd_bmp);
    }

    if (NULL != (void *)(((headerHandle_t *)header)->fileHeader)) {
        free(((headerHandle_t *)header)->fileHeader);
        ((headerHandle_t *)header)->fileHeader = NULL;
    }

    if (NULL != (void *)(((headerHandle_t *)header)->infoHeader)) {
        free(((headerHandle_t *)header)->infoHeader);
        ((headerHandle_t *)header)->fileHeader = NULL;
    }

    free(header);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int read_data(void *data, const void *header)
{
    SDK_LOG_V("enter");
    uint32_t absoluteIndex, sizeImage;
    int fd_bmp;
    size_t readLen;

    if (NULL == header) {
        return -1;
    }

    absoluteIndex = sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_);
    sizeImage = ((headerHandle_t *)header)->infoHeader->biSizeImage;
    fd_bmp = ((headerHandle_t *)header)->fd_bmp;

    switch (((headerHandle_t *)header)->infoHeader->biBitCount) {
        case BIT_COUNT_24:
            fs_seek(fd_bmp, absoluteIndex, SEEK_SET);
            break;

        case BIT_COUNT_8:
            fs_seek(fd_bmp, absoluteIndex + 4 * 256, SEEK_SET);
            break;

        case BIT_COUNT_4:
            fs_seek(fd_bmp, absoluteIndex + 4 * 16, SEEK_SET);
            break;

        case BIT_COUNT_1:
            fs_seek(fd_bmp, absoluteIndex + 4 * 2, SEEK_SET);
            break;

        default:
            fs_seek(fd_bmp, absoluteIndex, SEEK_SET);
            break;
    }

    readLen = fs_read(data, 1, sizeImage, fd_bmp);

    if (sizeImage != readLen) {
        SDK_LOG_V("assert func:%s, line:%d. ", __func__, __LINE__);
        return -1;
    }

    SDK_LOG_V("data %x %x %x %x. ", ((uint8_t *)data)[0], ((uint8_t *)data)[1], ((uint8_t *)data)[2], ((uint8_t *)data)[3]);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int read_color_table(void *data, const void *header)
{
    SDK_LOG_V("enter");
    uint32_t absoluteIndex, size;
    int fd_bmp;
    size_t readLen;

    if (NULL == header) {
        return -1;
    }

    absoluteIndex = sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_);
    fd_bmp = ((headerHandle_t *)header)->fd_bmp;

    switch (((headerHandle_t *)header)->infoHeader->biBitCount) {
        case BIT_COUNT_8:
            size = 4 * 256;
            break;

        case BIT_COUNT_4:
            size = 4 * 16;
            break;

        case BIT_COUNT_1:
            size = 4 * 2;
            break;

        default:
            size = 0;
            break;
    }

    fs_seek(fd_bmp, absoluteIndex, SEEK_SET);
    readLen = fs_read(data, 1, size, fd_bmp);

    if (size != readLen) {
        RTI_LOG("readLen %x size %x ", readLen, size);
        return -1;
    }

    SDK_LOG_V("data %x %x %x %x. ", ((uint8_t *)data)[0], ((uint8_t *)data)[1], ((uint8_t *)data)[2], ((uint8_t *)data)[3]);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int read_data_rect(void *data, void *rect, void *header)
{
    SDK_LOG_V("enter");
    uint32_t headerIndex, dataIndex, sizeImage;
    int fd_bmp;
    size_t readLen;

    if (NULL == header) {
        return -1;
    }

    headerIndex = sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_);
    fd_bmp = ((headerHandle_t *)header)->fd_bmp;

    dataIndex = ((bmpRect *)rect)->m_x + ((bmpRect *)rect)->m_y * ((headerHandle_t *)header)->infoHeader->biXPelsPerMeter;
    sizeImage = ((bmpRect *)rect)->width * ((bmpRect *)rect)->height * 3;
    fs_seek(fd_bmp, headerIndex + dataIndex, SEEK_SET);
    readLen = fs_read(data, 1, sizeImage, fd_bmp);

    if (sizeImage != readLen) {
        RTI_LOG("bmp sizeImage read error.");
        return -1;
    }

    SDK_LOG_V("leave");
    return 0;
}

#define READ_FROM_FLASH
#ifdef READ_FROM_FLASH
extern uint32_t extflash_read(uint8_t sspport, uint32_t flash_addr, uint32_t buf_addr, uint32_t size);
extern uint32_t flash_read_addr(const char *ptable_name);
OSI_SECTION_RW_KEEP int read_bmp_from_flash(const char *ptable_name, uint32_t buf_addr, uint32_t index, uint32_t size)
{
    uint32_t fls_addr = 0;
    int res = 0;
    fls_addr = flash_read_addr(ptable_name);
    res = extflash_read(EXT_FLASH_SELEXT, (unsigned int)(fls_addr + index), (unsigned int)buf_addr, size);
    SDK_LOG_V("name %s fls_addr 0x%x res %d size %d", ptable_name, fls_addr, res, size);
    return res;
}
#endif

OSI_SECTION_RW_KEEP int open_bitmap_loadTobuffer(const char *name, void **header)
{
    SDK_LOG_V("enter");

    headerHandle_t *headerHandle;
    //header
    headerHandle = (headerHandle_t *)malloc(sizeof(headerHandle_t));

    if (NULL == headerHandle) {
        SDK_LOG_V("malloc buf is null");
        return -1;
    }

    memset(headerHandle, 0, sizeof(headerHandle_t));
    //file header
    headerHandle->fileHeader = (BITMAPFILEHEADER_ *)malloc(sizeof(BITMAPFILEHEADER_));

    if (NULL == headerHandle->fileHeader) {
        SDK_LOG_V("malloc buf is null");
        return -1;
    }

    memset(headerHandle->fileHeader, 0, sizeof(BITMAPFILEHEADER_));
    //infoheader
    headerHandle->infoHeader = (BITMAPINFOHEADER_ *)malloc(sizeof(BITMAPINFOHEADER_));

    if (NULL == headerHandle->infoHeader) {
        SDK_LOG_V("malloc buf is null");
        return -1;
    }

    memset(headerHandle->infoHeader, 0, sizeof(BITMAPINFOHEADER_));
#ifndef READ_FROM_FLASH
    size_t readLen;
    headerHandle->fd_bmp =  fs_open(name, "rb+");
    // headerHandle->fd_bmp = open(name, O_RDONLY);

    if (headerHandle->fd_bmp <= 0) {
        SDK_LOG_V("open '%s' failed", name);
        return -1;
    }

    readLen = fs_read(headerHandle->fileHeader, 1, sizeof(BITMAPFILEHEADER_), headerHandle->fd_bmp);

    if (sizeof(BITMAPFILEHEADER_) != readLen) {
        SDK_LOG_V("read BITMAPFILEHEADER_ failed");
        return -1;
    }

#else
    read_bmp_from_flash(name, (unsigned int)headerHandle->fileHeader, 0, sizeof(BITMAPFILEHEADER_));
#endif

    if (headerHandle->fileHeader->bfType != 0x4d42) { //BM
        SDK_LOG_V("format:%s.The image isn`t bmp format. ", &headerHandle->fileHeader->bfType);
        return -1;
    }

    uint32_t absoluteIndex, sizeImage, colorBufferSize;
#ifndef READ_FROM_FLASH
    readLen = fs_read(headerHandle->infoHeader, 1, sizeof(BITMAPINFOHEADER_), headerHandle->fd_bmp);

    if (sizeof(BITMAPINFOHEADER_) != readLen) {
        SDK_LOG_V("read BITMAPINFOHEADER_ failed");
        return -1;
    }

#else
    read_bmp_from_flash(name, (unsigned int)headerHandle->infoHeader, sizeof(BITMAPFILEHEADER_), sizeof(BITMAPINFOHEADER_));
#endif
    absoluteIndex = sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_);
    sizeImage = headerHandle->infoHeader->biSizeImage;

    switch (headerHandle->infoHeader->biBitCount) {
        case BIT_COUNT_24:
            colorBufferSize = 0;
            break;

        case BIT_COUNT_8:
            colorBufferSize = 4 * 256;
            break;

        case BIT_COUNT_4:
            colorBufferSize = 4 * 16;
            break;

        case BIT_COUNT_1:
            colorBufferSize = 4 * 2;
            break;

        default:
            colorBufferSize = 0;
            break;
    }

    SDK_LOG_V("colorBufferSize 0x%x", colorBufferSize);

    if (0 != colorBufferSize) {
#ifndef READ_FROM_FLASH
        fs_seek(headerHandle->fd_bmp, absoluteIndex, SEEK_SET);
#endif
        headerHandle->colorTable = (RGBQUAD_ *)malloc(colorBufferSize);

        if (NULL == headerHandle->colorTable) {
            SDK_LOG_V("malloc buf is null");
            return -1;
        }

        SDK_LOG_V("headerHandle->colorTable %x", headerHandle->colorTable);
        memset(headerHandle->colorTable, 0, colorBufferSize);
#ifndef READ_FROM_FLASH
        readLen = fs_read(headerHandle->colorTable, 1, colorBufferSize, headerHandle->fd_bmp);

        if ((colorBufferSize) != readLen) {
            SDK_LOG_W("colorBufferSize 0x%lx", colorBufferSize);
            return -1;
        }

#else
        read_bmp_from_flash(name, (unsigned int)headerHandle->colorTable, absoluteIndex, colorBufferSize);
#endif
        SDK_LOG_V("colorBufferSize 0x%x %x %x ", colorBufferSize, ((uint8_t *)(headerHandle->colorTable))[0],
                  ((uint8_t *)(headerHandle->colorTable))[1]);
        SDK_LOG_V("colorBufferSize 0x%x %x %x %x", colorBufferSize, headerHandle->colorTable[0].blue,
                  headerHandle->colorTable[0].green, headerHandle->colorTable[0].red);
    }

#ifndef READ_FROM_FLASH
    fs_seek(headerHandle->fd_bmp, absoluteIndex + colorBufferSize, SEEK_SET);
#endif
    SDK_LOG_V("headerHandle->infoHeader->biSizeImage 0x%x", headerHandle->infoHeader->biSizeImage);
    headerHandle->data = (uint8_t *)malloc(headerHandle->infoHeader->biSizeImage);

    if (NULL == headerHandle->data) {
        SDK_LOG_V("malloc buf is null");
        return -1;
    }

    memset(headerHandle->data, 0, headerHandle->infoHeader->biSizeImage);
#ifndef READ_FROM_FLASH
    readLen = fs_read(headerHandle->data, 1, sizeImage, headerHandle->fd_bmp);

    if ((sizeImage) != readLen) {
        SDK_LOG_W("sizeImage 0x%lx", sizeImage);
        return -1;
    }

#else
    read_bmp_from_flash(name, (unsigned int)headerHandle->data, absoluteIndex + colorBufferSize, sizeImage);
#endif
    SDK_LOG_V("sizeImage 0x%x %x %x", sizeImage, ((uint8_t *)(headerHandle->data))[0],
              ((uint8_t *)(headerHandle->data))[1]);

    *header = headerHandle;
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int close_bitmap_loadTobuffer(void *header)
{
    if (NULL == header) {
        SDK_LOG_V("no font pointer to close!");
        return -1;
    }

    if (NULL != (void *)(((headerHandle_t *)header)->fd_bmp)) {
        fs_close(((headerHandle_t *)header)->fd_bmp);
    }

    if (NULL != (void *)(((headerHandle_t *)header)->fileHeader)) {
        free(((headerHandle_t *)header)->fileHeader);
        ((headerHandle_t *)header)->fileHeader = NULL;
    }

    if (NULL != (void *)(((headerHandle_t *)header)->infoHeader)) {
        free(((headerHandle_t *)header)->infoHeader);
        ((headerHandle_t *)header)->fileHeader = NULL;
    }

    if (NULL != (void *)(((headerHandle_t *)header)->colorTable)) {
        free(((headerHandle_t *)header)->colorTable);
        ((headerHandle_t *)header)->colorTable = NULL;
    }

    if (NULL != (void *)(((headerHandle_t *)header)->data)) {
        free(((headerHandle_t *)header)->data);
        ((headerHandle_t *)header)->data = NULL;
    }

    free(header);

    return 0;
}

/*hzr 开机logo*/
static headerHandle_t *logo_headerHandle = NULL;
static void *logo_paint_buffer = NULL;
extern uint32_t lcd_hal_get_id(void);
extern void lcd_hal_get_size(uint32_t *height, uint32_t *width, uint32_t *bits);
OSI_SECTION_RW_KEEP uint16_t rgb888_to_rgb565_for_hzrlogo(uint32_t color)
{
    uint16_t r = (uint16_t)((color & 0xff0000) >> 16);
    uint16_t g = (uint16_t)((color & 0x00ff00) >> 8);
    uint16_t b = (uint16_t)(color & 0x0000ff);

    r >>= 3;
    g >>= 2;
    b >>= 3;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

OSI_SECTION_RW_KEEP void *read_bitmap_for_hzrlogo(const char *name)
{
    SDK_LOG_V("enter");
    size_t readLen;
    (void)readLen;
    //header
    logo_headerHandle = (headerHandle_t *)malloc(sizeof(headerHandle_t));

    if (NULL == logo_headerHandle) {
        SDK_LOG_W("malloc buf is null");
        goto func_out;
    }

    memset(logo_headerHandle, 0, sizeof(headerHandle_t));
    //file header
    logo_headerHandle->fileHeader = (BITMAPFILEHEADER_ *)malloc(sizeof(BITMAPFILEHEADER_));

    if (NULL == logo_headerHandle->fileHeader) {
        SDK_LOG_W("malloc buf is null");
        goto func_out;
    }

    memset(logo_headerHandle->fileHeader, 0, sizeof(BITMAPFILEHEADER_));
    //infoheader
    logo_headerHandle->infoHeader = (BITMAPINFOHEADER_ *)malloc(sizeof(BITMAPINFOHEADER_));

    if (NULL == logo_headerHandle->infoHeader) {
        SDK_LOG_W("malloc buf is null");
        goto func_out;
    }

    memset(logo_headerHandle->infoHeader, 0, sizeof(BITMAPINFOHEADER_));
    logo_headerHandle->fd_bmp = fs_open(name, "rb+");
    // logo_headerHandle->fd_bmp = open(name, O_RDONLY);

    if (logo_headerHandle->fd_bmp <= 0) {
        SDK_LOG_W("open '%s' failed", name);
        goto func_out;
    } else {
        SDK_LOG_I("open '%s' success", name);
    }

    fs_seek(logo_headerHandle->fd_bmp, 8, SEEK_SET); /*hzr msg*/
    readLen = fs_read(logo_headerHandle->fileHeader, 1, sizeof(BITMAPFILEHEADER_), logo_headerHandle->fd_bmp);

    if (sizeof(BITMAPFILEHEADER_) != readLen) {
        SDK_LOG_V("read BITMAPFILEHEADER_ failed %d %d %d", sizeof(BITMAPFILEHEADER_), readLen, logo_headerHandle->fd_bmp);
        goto func_out;
    }

    uint32_t absoluteIndex = 0, sizeImage = 0, colorBufferSize = 0;
    uint32_t seek ;
    (void)seek;
    seek = fs_seek(logo_headerHandle->fd_bmp, 8 + sizeof(BITMAPFILEHEADER_), SEEK_SET); /*hzr*/
    SDK_LOG_V("seek %d", seek);
    readLen = fs_read(logo_headerHandle->infoHeader, 1, sizeof(BITMAPINFOHEADER_), logo_headerHandle->fd_bmp);

    if (sizeof(BITMAPINFOHEADER_) != readLen) {
        SDK_LOG_V("read BITMAPINFOHEADER_ failed %d %d", sizeof(BITMAPINFOHEADER_), readLen);
        goto func_out;
    }

    // uint32_t total_size = fs_size(name);
    uint32_t total_size = fs_seek(logo_headerHandle->fd_bmp, 0, SEEK_END);
    SDK_LOG_V("total_size 0x%x", total_size);
    absoluteIndex = 8 + sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_);

    // if (logo_headerHandle->infoHeader->biBitCount == 8) {
    colorBufferSize = 4 * 256;
    // }

    // sizeImage =  logo_headerHandle->infoHeader->biSizeImage; /*hzr read is 0*/
    sizeImage = total_size - absoluteIndex - colorBufferSize;
    SDK_LOG_I("biBitCount %d  sizeImage 0x%x total_size 0x%lx", logo_headerHandle->infoHeader->biBitCount, sizeImage,
              total_size);
    // if (0 != colorBufferSize) {
    seek = fs_seek(logo_headerHandle->fd_bmp, absoluteIndex, SEEK_SET);
    SDK_LOG_V("seek %d", seek);
    logo_headerHandle->colorTable = (RGBQUAD_ *)malloc(colorBufferSize);

    if (NULL == logo_headerHandle->colorTable) {
        SDK_LOG_W("malloc buf is null");
        goto func_out;
    }

    memset(logo_headerHandle->colorTable, 0, colorBufferSize);
    readLen = fs_read((logo_headerHandle->colorTable), 1, colorBufferSize, logo_headerHandle->fd_bmp);
    SDK_LOG_V("0x%x %x", colorBufferSize, (logo_headerHandle->colorTable));
    SDK_LOG_V("colorBufferSize 0x%lx %x %x %x", colorBufferSize,
              logo_headerHandle->colorTable[0].blue,
              logo_headerHandle->colorTable[0].green,
              logo_headerHandle->colorTable[0].red);

    if ((colorBufferSize) != readLen) {
        SDK_LOG_W("colorBufferSize 0x%lx", colorBufferSize);
        goto func_out;
    }

    // }
    logo_headerHandle->data = (uint8_t *)malloc(sizeImage);

    if (NULL == logo_headerHandle->data) {
        SDK_LOG_W("malloc buf is null");
        goto func_out;
    }

    fs_seek(logo_headerHandle->fd_bmp, absoluteIndex + colorBufferSize, SEEK_SET);
    memset(logo_headerHandle->data, 0, sizeImage);
    readLen = fs_read(logo_headerHandle->data, 1, sizeImage, logo_headerHandle->fd_bmp);
    SDK_LOG_V("sizeImage 0x%lx %x %x", sizeImage, ((uint8_t *)(logo_headerHandle->data))[0],
              ((uint8_t *)(logo_headerHandle->data))[1]);

    if ((sizeImage) != readLen) {
        SDK_LOG_W("sizeImage 0x%lx", sizeImage);
        goto func_out;
    }

    uint8_t *dataBuff = NULL;
    uint32_t h = 0, w = 0;
    dataBuff = (uint8_t *)logo_headerHandle->data;
    w = logo_headerHandle->infoHeader->biWidth;
    h = logo_headerHandle->infoHeader->biHeight;
    SDK_LOG_V("biWidth %d biHeight %d biBitCount %d", w, h, logo_headerHandle->infoHeader->biBitCount);

    /*lcd size, fix 128*160*/
    uint32_t lcd_h = 128, lcd_w = 160;
    lcd_hal_get_size(&lcd_w, &lcd_h, NULL); /*need support 128*160 & 240*320*/

    logo_paint_buffer = (uint16_t *)malloc(lcd_h *lcd_w * 2); /*for rgb565*/

    if (logo_paint_buffer == NULL) {
        SDK_LOG_W("malloc logo_paint_buffer failed");
        goto func_out;
    }

    memset(logo_paint_buffer, 0, lcd_h *lcd_w * 2);

    uint32_t id = 0;
    id = lcd_hal_get_id();

    if (logo_headerHandle->infoHeader->biBitCount == 8) {
        RGBQUAD_ *cBuff = NULL;
        cBuff = (((RGBQUAD_ *)(logo_headerHandle->colorTable)));
        int swallow = 0 ;

        if ((swallow = ((w) % 4)) != 0) {
            swallow = 4 - swallow;
        } else {
            swallow = 0 ;
        }

        for (int temp_y = 0; temp_y < lcd_h; temp_y++) {
            for (int temp_x = 0; temp_x < lcd_w; temp_x++) {
                int index = ((uint8_t *)dataBuff)[(h - 1 - temp_y) * w + temp_x + (h - 1 - temp_y) * swallow];
                uint32_t color = ((uint32_t)(((RGBQUAD_ *)cBuff)[index].blue)) | ((uint32_t)(((RGBQUAD_ *)cBuff)[index].green) << 8)
                                 | ((
                                        uint32_t)(((RGBQUAD_ *)cBuff)[index].red) << 16);
                color = rgb888_to_rgb565_for_hzrlogo(color);

                if (id == 0x077353) {
                    int pos_x = temp_x;
                    int pos_y = temp_y;
                    int new_x = pos_y;
                    int new_y = lcd_w - pos_x - 1;
                    ((uint16_t *)logo_paint_buffer)[new_y * (lcd_h) + new_x] = fill_color(color);
                } else if (id == 0x773532) {
                    int pos_x = temp_x;
                    int pos_y = temp_y;
                    int new_x = lcd_h - pos_y - 1;
                    int new_y = pos_x;
                    // int pos_x = temp_x;
                    // int pos_y = temp_y;
                    // int new_x = pos_y;
                    // int new_y = lcd_w - pos_x - 1;
                    ((uint16_t *)logo_paint_buffer)[new_y * (lcd_h) + new_x] = fill_color(color);
                } else {
                    int pos_x = temp_x;
                    int pos_y = temp_y;
                    ((uint16_t *)logo_paint_buffer)[pos_y * (lcd_h) + pos_x] = fill_color(color);
                }
            }
        }
    }

func_out:

    if (logo_headerHandle->fd_bmp != -1) {
        close(logo_headerHandle->fd_bmp);
    }

    if (logo_headerHandle->fileHeader != NULL) {
        free(logo_headerHandle->fileHeader);
    }

    if (logo_headerHandle->infoHeader != NULL) {
        free(logo_headerHandle->infoHeader);
    }

    if (logo_headerHandle->colorTable != NULL) {
        free(logo_headerHandle->colorTable);
    }

    if (logo_headerHandle->data != NULL) {
        free(logo_headerHandle->data);
    }

    if (logo_headerHandle != NULL) {
        free(logo_headerHandle);
    }

    SDK_LOG_V("leave");
    return (uint8_t *)logo_paint_buffer;
}

OSI_SECTION_RW_KEEP int free_bitmap_for_hzrlogo(void)
{
    if (logo_headerHandle->colorTable != NULL) {
        free(logo_headerHandle->colorTable);
    } else {
        SDK_LOG_V("colorTable id null");
    }

    if (logo_headerHandle->data != NULL) {
        free(logo_headerHandle->data);
    } else {
        SDK_LOG_V("data is null");
    }

    if (logo_headerHandle->fileHeader != NULL) {
        free(logo_headerHandle->fileHeader);
    } else {
        SDK_LOG_V("fileHeader is null");
    }

    if (logo_headerHandle->infoHeader != NULL) {
        free(logo_headerHandle->infoHeader);
    } else {
        SDK_LOG_V("infoHeader is null");
    }

    if (logo_paint_buffer != NULL) {
        free(logo_paint_buffer);
    } else {
        SDK_LOG_V("logo_paint_buffer is null");
    }

    fs_close(logo_headerHandle->fd_bmp);

    if (logo_headerHandle != NULL) {
        free(logo_headerHandle);
    } else {
        SDK_LOG_V("fd_bmp is null");
    }

    return 0;
}

//paint.c
typedef struct {
    paintRange_t paintPixel;
    textRect rectangle;
    int bits_per_pixel;
    uint16_t back_color; //rgb565
    uint16_t fore_color;
    void *colorBuff; //rgb565 send to lcd
    void *back_colorBuff;
    void *sys_msg_colorbuffer;//for system message
    void *buf_to_user; //rgb565
    osiSemaphore_t *sema_backbuf_lock;
} paintHandle_t;

OSI_SECTION_RW_KEEP static void draw_char(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *chr,
        uint8_t layer, const font_t pfont, const Paint_t paint);
OSI_SECTION_RW_KEEP int paint_draw_rect_paint(int x, int y, uint32_t width, uint32_t height, uint16_t buffer,
        uint8_t layer, const Paint_t paint);

#ifdef USE_NOR_FLS
#define NOR_FLS_FONT_MINI     "Charset_ascii_mini"
#define NOR_FLS_FONT_NORMAL   "Charset_ascii_normal"

extern uint32_t extflash_read(uint8_t sspport, uint32_t flash_addr, uint32_t buf_addr, uint32_t size);
extern uint32_t flash_read_addr(const char *ptable_name);
OSI_SECTION_RW_KEEP static int read_font_from_norflash(uint32_t buf_addr, uint32_t index, uint32_t size,
        const font_t pfont)
{
    uint32_t fls_addr = 0;
    int res = 0;

    if (((fontHandle_t *)pfont)->font_size == 12) {
        fls_addr = flash_read_addr(NOR_FLS_FONT_MINI);
    } else if (((fontHandle_t *)pfont)->font_size == 16) {
        fls_addr = flash_read_addr(NOR_FLS_FONT_NORMAL);
    } else if (((fontHandle_t *)pfont)->font_size == 24) {
        fls_addr = flash_read_addr(NOR_FLS_FONT_NORMAL);
    } else if (((fontHandle_t *)pfont)->font_size == 40) {
        fls_addr = flash_read_addr(NOR_FLS_FONT_NORMAL);
    } else {
        fls_addr = flash_read_addr(NOR_FLS_FONT_NORMAL);
        // SDK_LOG_E("invalid font size: %d", ((fontHandle_t *)pfont)->font_size);
        // return -1;
    }

    res = extflash_read(EXT_FLASH_SELEXT, (unsigned int)(fls_addr + index), (unsigned int)buf_addr, size);
    SDK_LOG_V("fls_addr 0x%x res %d size %d", fls_addr, res, ((fontHandle_t *)pfont)->font_size);
    return res;
}

#endif

//backlight 0:off 1:on
int g_backlight_status = 0;
OSI_SECTION_RW_KEEP int paint_create_user(int width, int height, int bits_per_pixel, Paint_t *paint)
{
    SDK_LOG_V("enter");
    paintHandle_t *handle;
    handle = (paintHandle_t *)malloc(sizeof(paintHandle_t));
    memset(handle, 0x00, sizeof(paintHandle_t));
    handle->paintPixel.width = (uint16_t)width;
    handle->paintPixel.height = (uint16_t)height;
    handle->bits_per_pixel = bits_per_pixel;
    handle->buf_to_user = NULL;

    switch (bits_per_pixel) {
        case 8:
        case 16:
        default:
            handle->colorBuff = alloc_framebuffer_memory(width, height);

            if (NULL == handle->colorBuff) {
                SDK_LOG_V("malloc colorBuff space fail!");
                return -1;
            }

            handle->fore_color = 0x0000;
            handle->back_color = 0xffff;
            break;
    }

    *paint = handle;
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_get_size_user(uint32_t *width, uint32_t *height, const Paint_t paint)
{
    if (paint == NULL) {
        return -1;
    }

    if (width != NULL) {
        *width = ((paintHandle_t *)paint)->paintPixel.width;
    }

    if (height != NULL) {
        *height = ((paintHandle_t *)paint)->paintPixel.height;
    }

    return 0;
}

OSI_SECTION_RW_KEEP int paint_destory_user(const Paint_t paint)
{
    SDK_LOG_V("enter");
    int res = 0;

    if (NULL == paint) {
        SDK_LOG_E("no paint need to be destroyed!");
        return -1;
    }

    res = free_framebuffer_memory((void *)((paintHandle_t *)paint)->colorBuff);

    if (res == 0) {
        ((paintHandle_t *)paint)->colorBuff = NULL;
    }

    res = free_framebuffer_memory((void *)((paintHandle_t *)paint)->buf_to_user);

    if (res == 0) {
        ((paintHandle_t *)paint)->buf_to_user = NULL;
    }

    free(paint);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_clean_rect_user_with_layer(int x, int y, uint32_t width, uint32_t height,
        uint8_t layer, const Paint_t paint)
{
    SDK_LOG_V("enter");

    if (NULL == paint) {
        SDK_LOG_E("paint is null");
        return -1;
    }

    if (x <= 0) {
        x = 0;
    }

    if (y <= 0) {
        y = 0;
    }

    if (x >= ((paintHandle_t *)paint)->paintPixel.width) {
        x = ((paintHandle_t *)paint)->paintPixel.width - 1;
    }

    if (y >= ((paintHandle_t *)paint)->paintPixel.height) {
        y = ((paintHandle_t *)paint)->paintPixel.height - 1;
    }

    if (width >= ((paintHandle_t *)paint)->paintPixel.width) {
        width = ((paintHandle_t *)paint)->paintPixel.width;
    }

    if (width == 0) {
        width = 1;
    }

    if (height >= ((paintHandle_t *)paint)->paintPixel.height) {
        height = ((paintHandle_t *)paint)->paintPixel.height;
    }

    if (height == 0) {
        height = 1;
    }

    ((paintHandle_t *)paint)->rectangle.m_x = x;
    ((paintHandle_t *)paint)->rectangle.m_y = y;
    paint_draw_rect_paint(x, y, width, height, ((paintHandle_t *)paint)->back_color, layer, paint);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_clean_rect_user(int x, int y, uint32_t width, uint32_t height, const Paint_t paint)
{
    int ret;
    ret = paint_clean_rect_user_with_layer(x, y, width, height, 0, paint);
    return ret;
}

OSI_SECTION_RW_KEEP int paint_draw_pixel_user(int x, int y, const Paint_t paint)
{
    SDK_LOG_V("enter");

    if (NULL == paint) {
        SDK_LOG_W("need creat paint!");
        return -1;
    }

    if ((x >= ((paintHandle_t *)paint)->paintPixel.width) || (y >= ((paintHandle_t *)paint)->paintPixel.height) || (x < 0)
        || (y < 0)) {
        SDK_LOG_V("(x, y) Position is out of range!");
        return -1;
    }

    paint_draw_rect_paint(x, y, 1, 1, ((paintHandle_t *)paint)->fore_color, 0, paint);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_get_pixel_user(int x, int y, uint32_t *color, const Paint_t paint)
{
    SDK_LOG_V("enter");

    if ((color == NULL) || (paint == NULL)) {
        SDK_LOG_W("pointer is null!");
        return -1;
    }

    if ((x >= ((paintHandle_t *)paint)->paintPixel.width) || (y >= ((paintHandle_t *)paint)->paintPixel.height) || (x < 0)
        || (y < 0)) {
        SDK_LOG_W("(%d, %d) position is out of range!", x, y);
        return -1;
    }

    uint16_t *colorBuff = (uint16_t *)((paintHandle_t *)paint)->colorBuff;
    uint32_t h, w;
    uint32_t temp_color;
    lcd_get_size(&h, &w, NULL);

    switch (lcd_get_direction()) {
        case LCD_TRANSFORM_ROTATE_270: {
            int pos_x = x;
            int pos_y = y;
            int new_x = pos_y;
            int new_y = w - pos_x - 1;
            temp_color = colorBuff[new_y * (h) + new_x];
            break;
        }

        case LCD_TRANSFORM_ROTATE_90: {
            int pos_x = x;
            int pos_y = y;
            int new_x = h - pos_y - 1;
            int new_y = pos_x;
            temp_color = colorBuff[new_y * (h) + new_x];
            break;
        }

        case LCD_TRANSFORM_MIRROR_X: {
            int pos_x = x;
            int pos_y = h - y - 1;
            temp_color = colorBuff[pos_y * (h) + pos_x];
            break;
        }

        default: {
            int pos_x = x;
            int pos_y = y;
            temp_color = colorBuff[pos_y * (h) + pos_x];
            break;
        }
    }

    temp_color = rgb565_to_rgb888(temp_color);
    *color = temp_color;
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_set_color_user(uint32_t back_color, uint32_t fore_color, Paint_t paint)
{
    SDK_LOG_V("enter");
    uint16_t rgb565_back_color, rgb565_fore_color;

    if (NULL == paint) {
        SDK_LOG_E("paint is null");
        return -1;
    }

    rgb565_back_color = rgb888_to_rgb565(back_color);
    rgb565_fore_color = rgb888_to_rgb565(fore_color);
    ((paintHandle_t *)paint)->back_color = rgb565_back_color;
    ((paintHandle_t *)paint)->fore_color = rgb565_fore_color;
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_get_color_user(uint32_t *back_color, uint32_t *fore_color, const Paint_t paint)
{
    SDK_LOG_V("enter");

    if ((back_color == NULL) || (fore_color == NULL)) {
        SDK_LOG_E("buffer is null!");
        return -1;
    }

    if (NULL == paint) {
        SDK_LOG_E("need creat paint!");
        return -1;
    }

    *back_color = rgb565_to_rgb888(((paintHandle_t *)paint)->back_color);
    *fore_color = rgb565_to_rgb888(((paintHandle_t *)paint)->fore_color);
    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_draw_picturefile_user(int x, int y, const void *m_pic, const Paint_t paint)
{
    SDK_LOG_V("enter");
    uint8_t *dataBuff = NULL;
    int32_t w, h, color, index, temp_x, temp_y;

    if (paint == NULL) {
        SDK_LOG_W("paint handle is invalid.");
        return -1;
    }

    /* 判断BITMAPFILEHEADER_->bfType !=  'BM' */
    if (*(uint16_t *)((uint8_t *)m_pic) != 0x4d42) {
        SDK_LOG_W("fault BMP type!.");
        return -1;
    }

    w = *(uint32_t *)((uint8_t *)m_pic + 18);
    h = *(uint32_t *)((uint8_t *)m_pic + 22);
    SDK_LOG_V("W %d h %d biBitCount %d", w, h, (*(uint16_t *)((uint8_t *)m_pic + 28)));

    switch (*(uint16_t *)((uint8_t *)m_pic + 28)) {
        case 1: {
            dataBuff = (uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_)) + 4 * 2;
            RGBQUAD_ cBuff[2];
            // (void)read_color_table(cBuff, bmpHandle); // 读取颜色表
            memcpy(cBuff, ((uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_))), sizeof(RGBQUAD_) * 2);
            int bytes_per_row = ((w + 31) >> 5) * 4;
            SDK_LOG_V("bytes_per_row %d", bytes_per_row);

            for (int temp_y = 0; temp_y < h; temp_y++) {
                for (int temp_x = 0; temp_x < w; temp_x++) {
                    // int byte_index = (h - 1 - temp_y) * bytes_per_row + temp_x / 8;
                    int byte_index = (h - 1 - temp_y) * bytes_per_row + (temp_x >> 3);
                    int bit_position = 7 - (temp_x % 8);
                    uint8_t pixel_data = dataBuff[byte_index];
                    int pixel_value = (pixel_data >> bit_position) & 0x01;
                    uint32_t color = ((uint32_t)(cBuff[pixel_value].blue)) |
                                     ((uint32_t)(cBuff[pixel_value].green) << 8) |
                                     ((uint32_t)(cBuff[pixel_value].red) << 16);
                    color = rgb888_to_rgb565(color);

                    if (((x + temp_x) >= 0) && ((x + temp_x) < ((paintHandle_t *)paint)->paintPixel.width) && \
                        ((y + temp_y) >= 0) && ((y + temp_y) < ((paintHandle_t *)paint)->paintPixel.height)) {
                        paint_draw_rect_paint(x + temp_x, y + temp_y, 1, 1, color, 0, paint);
                    }
                }
            }
        }
        break;

        case 4: {
            dataBuff = (uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_)) + 4 * 16;
            RGBQUAD_ cBuff[16];
            memcpy(cBuff, ((uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_))), sizeof(RGBQUAD_) * 16);
            int bytes_per_row = (((w * 4 + 7) >> 3) + 3) & ~3;
            SDK_LOG_V("bytes_per_row %d", bytes_per_row);

            for (int temp_y = 0; temp_y < h; temp_y++) {
                for (int temp_x = 0; temp_x < w; temp_x++) {
                    int byte_index = (h - 1 - temp_y) * bytes_per_row + (temp_x >> 1);
                    int bit_position = 4 * (1 - (temp_x % 2)); // 4位深度的像素数据的高4位或低4位
                    uint8_t pixel_data = dataBuff[byte_index];
                    int pixel_value = (pixel_data >> bit_position) & 0x0F;
                    uint32_t color = ((uint32_t)(cBuff[pixel_value].blue)) |
                                     ((uint32_t)(cBuff[pixel_value].green) << 8) |
                                     ((uint32_t)(cBuff[pixel_value].red) << 16);
                    color = rgb888_to_rgb565(color);

                    if (((x + temp_x) >= 0) && ((x + temp_x) < ((paintHandle_t *)paint)->paintPixel.width) && \
                        ((y + temp_y) >= 0) && ((y + temp_y) < ((paintHandle_t *)paint)->paintPixel.height)) {
                        paint_draw_rect_paint(x + temp_x, y + temp_y, 1, 1, color, 0, paint);
                    }
                }
            }
        }
        break;

        case 8: {
            dataBuff = (uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_)) + 4 * 256;
            RGBQUAD_ cBuff[256];
            memcpy(cBuff, ((uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_))), sizeof(RGBQUAD_) * 256);
            int swallow = 0 ;

            if ((swallow = ((w) % 4)) != 0) {
                swallow = 4 - swallow;
            } else {
                swallow = 0;
            }

            for (temp_y = 0; temp_y < h; temp_y++) {
                for (temp_x = 0; temp_x < w; temp_x++) {
                    index = dataBuff[(h - 1 - temp_y) * w + temp_x + (h - 1 - temp_y) * swallow];
                    color = ((uint32_t)(cBuff[index].blue)) | ((uint32_t)(cBuff[index].green) << 8) | ((uint32_t)(cBuff[index].red) << 16);
                    color = rgb888_to_rgb565(color);

                    if (((x + temp_x) >= 0) && ((x + temp_x) < ((paintHandle_t *)paint)->paintPixel.width) && \
                        ((y + temp_y) >= 0) && ((y + temp_y) < ((paintHandle_t *)paint)->paintPixel.height)) {
                        paint_draw_rect_paint(x + temp_x, y + temp_y, 1, 1, color, 0, paint);
                    }
                }
            }
        }
        break;

        case 16: {
            dataBuff = (uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_));

            for (temp_x = 0; temp_x < w; temp_x++) {
                for (temp_y = 0; temp_y < h; temp_y++) {
                    index = (h - 1 - temp_y) * w * 2 + temp_x * 2;
                    color = ((uint32_t)(dataBuff[index])) | ((uint32_t)(dataBuff[index + 1]) << 8);

                    if (((x + temp_x) >= 0) && ((x + temp_x) < ((paintHandle_t *)paint)->paintPixel.width) && \
                        ((y + temp_y) >= 0) && ((y + temp_y) < ((paintHandle_t *)paint)->paintPixel.height)) {
                        paint_draw_rect_paint(x + temp_x, y + temp_y, 1, 1, color, 0, paint);
                    }
                }
            }
        }
        break;

        case 24:
        default: {
            dataBuff = (uint8_t *)m_pic + (sizeof(BITMAPFILEHEADER_) + sizeof(BITMAPINFOHEADER_));

            for (temp_x = 0; temp_x < w; temp_x++) {
                for (temp_y = 0; temp_y < h; temp_y++) {
                    index = (h - 1 - temp_y) * w * 3 + temp_x * 3;
                    color = ((uint32_t)(dataBuff[index])) | ((uint32_t)(dataBuff[index + 1]) << 8) | ((uint32_t)(
                                dataBuff[index + 2]) << 16);
                    color = rgb888_to_rgb565(color);

                    if (((x + temp_x) >= 0) && ((x + temp_x) < ((paintHandle_t *)paint)->paintPixel.width) && \
                        ((y + temp_y) >= 0) && ((y + temp_y) < ((paintHandle_t *)paint)->paintPixel.height)) {
                        paint_draw_rect_paint(x + temp_x, y + temp_y, 1, 1, color, 0, paint);
                    }
                }
            }
        }
        break;
    }

    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP int paint_draw_text_user_with_layer(font_t pfont, int x, int y, const char *lpText,
        uint8_t layer, const Paint_t paint)
{
    SDK_LOG_V("enter");

    uint16_t temp_x = 0, temp_y = 0;
    int tempReturn = 0;

    if (pfont == NULL) {
        SDK_LOG_W("font handle is invalid.");
        return -1;
    }

    if (paint == NULL) {
        SDK_LOG_W("paint handle is invalid.");
        return -1;
    }

    while (*lpText != '\0') {

        if ((x + temp_x) >= ((paintHandle_t *)paint)->paintPixel.width) {
            temp_x = 0;
            temp_y += ((fontHandle_t *)pfont)->font_height;
        }
        SDK_LOG_V(">>>> *lpText 0x%x (y + temp_y) %d  %d (x + temp_x) %d  %d", *lpText, (y + temp_y), ((paintHandle_t *)paint)->paintPixel.height, (x + temp_x), ((paintHandle_t *)paint)->paintPixel.width);
        if ((y + temp_y) >= ((paintHandle_t *)paint)->paintPixel.height) {
            SDK_LOG_E("reaching the maximum size.");
            break;
        } else {
            if ((((x + temp_x) >= 0) && ((x + temp_x) < ((paintHandle_t *)paint)->paintPixel.width)) && \
                (((y + temp_y) >= 0) && ((y + temp_y) < ((paintHandle_t *)paint)->paintPixel.height))) {
                SDK_LOG_V("*lpText 0x%x addr 0x%x", *lpText, lpText);
                draw_char(x + temp_x, y + temp_y, \
                          ((paintHandle_t *)paint)->paintPixel.width, ((paintHandle_t *)paint)->paintPixel.height, \
                          lpText, layer, pfont, paint);
            }

            if (*lpText < 0x80) {
                temp_x += ((fontHandle_t *)pfont)->font_size >> 1;
                lpText++;
            } else {
                temp_x += ((fontHandle_t *)pfont)->font_size;
                lpText++;
                lpText++;
            }
        }
    }

    SDK_LOG_V("leave");
    return tempReturn;
}

OSI_SECTION_RW_KEEP int paint_draw_text_user(font_t pfont, int x, int y, const char *lpText, const Paint_t paint)
{
    int tempReturn = 0;

    tempReturn = paint_draw_text_user_with_layer(pfont, x, y, lpText, 0, paint);

    return tempReturn;
}

OSI_SECTION_RW_KEEP void *paint_get_buf_user(const Paint_t paint)
{
    uint32_t h = 0, w = 0;
    lcd_get_size(&h, &w, NULL);

    if (paint == NULL) {
        return NULL;
    }

    if (((paintHandle_t *)paint)->buf_to_user == NULL) {
        ((paintHandle_t *)paint)->buf_to_user = alloc_framebuffer_memory(w, h); //565 (2byte, uint16)

        if (NULL == ((paintHandle_t *)paint)->buf_to_user) {
            SDK_LOG_W("malloc buf_to_user space fail!");
            return NULL;
        }
    }

    uint16_t *colorBuff = (uint16_t *)((paintHandle_t *)paint)->colorBuff;

    switch (lcd_get_direction()) {
        case LCD_TRANSFORM_ROTATE_270: {
            for (int j = 0; j < (h); j++) {
                for (int i = 0; i < (w); i++) {
                    int pos_x = i;
                    int pos_y = j;
                    int new_x = pos_y;
                    int new_y = w - pos_x - 1;
                    ((uint16_t *)(((paintHandle_t *)paint)->buf_to_user))[pos_y *w + pos_x] =
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
                    ((uint16_t *)(((paintHandle_t *)paint)->buf_to_user))[pos_y *w + pos_x] =
                        (colorBuff[new_y * (h) + new_x]);
                }
            }

            break;
        }

        case LCD_TRANSFORM_MIRROR_X: {
            for (int j = 0; j < (h); j++) {
                for (int i = 0; i < (w); i++) {
                    int pos_x = i;
                    int pos_y = j;
                    int new_x = pos_x;
                    int new_y = h - pos_y - 1;
                    ((uint16_t *)(((paintHandle_t *)paint)->buf_to_user))[pos_y *w + pos_x] =
                        (colorBuff[new_y * (h) + new_x]);
                }
            }

            break;
        }

        default: {
            for (int j = 0; j < (h); j++) {
                for (int i = 0; i < (w); i++) {
                    int pos_x = i;
                    int pos_y = j;
                    int new_x = pos_x;
                    int new_y = pos_y;
                    ((uint16_t *)(((paintHandle_t *)paint)->buf_to_user))[pos_y *w + pos_x] =
                        (colorBuff[new_y * (h) + new_x]);
                }
            }

            break;
        }

    }

    return (uint8_t *)(((paintHandle_t *)paint)->buf_to_user);
}

OSI_SECTION_RW_KEEP int paint_free_buf_user(const Paint_t paint)
{
    SDK_LOG_V("enter");
    int res = 0;

    if (NULL == paint) {
        SDK_LOG_E("no paint need to be destroyed!");
        return -1;
    }

    res = free_framebuffer_memory((void *)((paintHandle_t *)paint)->buf_to_user);

    if (res == 0) {
        ((paintHandle_t *)paint)->buf_to_user = NULL;
    }

    SDK_LOG_V("leave");
    return res;
}

OSI_SECTION_RW_KEEP int paint_draw_text_rect_user(void *fd, int nX, int nY, void *rect, const char *lpText,
        Paint_t paint)
{
    SDK_LOG_V("enter");
    uint16_t temp_x = 0, temp_y = 0;
    int tempReturn = 0;

    if (fd == NULL) {
        SDK_LOG_E("font handle is invalid.");
        return -1;
    }

    if (paint == NULL) {
        SDK_LOG_E("paint handle is invalid.");
        return -1;
    }

    ((paintHandle_t *)paint)->rectangle.m_x = ((textRect *)rect)->m_x;
    ((paintHandle_t *)paint)->rectangle.m_y = ((textRect *)rect)->m_y;
    ((paintHandle_t *)paint)->rectangle.width = ((textRect *)rect)->width;
    ((paintHandle_t *)paint)->rectangle.height = ((textRect *)rect)->height;

    while (*lpText != '\0') {
        if ((temp_x + ((fontHandle_t *)fd)->font_width) > ((paintHandle_t *)paint)->rectangle.width) {
            temp_x = 0;
            temp_y += ((fontHandle_t *)fd)->font_height;
        }

        if ((temp_y + ((fontHandle_t *)fd)->font_height) >= ((paintHandle_t *)paint)->rectangle.height) {
            SDK_LOG_V("reaching the maximum size.");
            break;
        } else {
            if ((((nX + temp_x) >= 0) && ((nX + temp_x) < ((paintHandle_t *)paint)->paintPixel.width)) && \
                (((nY + temp_y) >= 0) && ((nY + temp_y) < ((paintHandle_t *)paint)->paintPixel.height))) {
                draw_char(nX + temp_x, nY + temp_y, \
                          ((paintHandle_t *)paint)->rectangle.width, ((paintHandle_t *)paint)->rectangle.height, \
                          lpText, 0, fd, paint);
            }

            if (*lpText < 0x80) {
                temp_x += ((fontHandle_t *)fd)->font_size >> 1;
                lpText++;
            } else {
                temp_x += ((fontHandle_t *)fd)->font_size;
                lpText++;
                lpText++;
            }
        }
    }

    SDK_LOG_V("leave");
    return tempReturn;
}

OSI_SECTION_RW_KEEP int paint_open_picture(const char *name, void **bmpHandle)
{
    int ret = 0;
    ret = open_bitmap(name, bmpHandle);

    if (-1 == ret) {
        SDK_LOG_E("bitmap open failed.");
        return -1;
    }

    return 0;
}

OSI_SECTION_RW_KEEP int paint_close_picture(void *bmpHandle)
{
    int ret = 0;
    ret = close_bitmap(bmpHandle);
    return ret;
}

extern void lcd_hal_refresh_done(void);
/* support user paint area*/
OSI_SECTION_RW_KEEP int paint_draw_rect_paint(int x, int y, uint32_t width, uint32_t height, uint16_t buffer,
        uint8_t layer, const Paint_t paint)
{
    SDK_LOG_V("enter");
    lcd_hal_refresh_done();
    SDK_LOG_V("start");
    uint32_t h, w;
    lcd_get_size(&h, &w, NULL);

    uint16_t *colorBuff = (uint16_t *)((paintHandle_t *)paint)->colorBuff;

    switch (layer) {
        case 0:
            colorBuff = (uint16_t *)((paintHandle_t *)paint)->colorBuff;
            break;

        case 1:
            colorBuff = (uint16_t *)((paintHandle_t *)paint)->back_colorBuff;
            break;

        case 2:
            colorBuff = (uint16_t *)((paintHandle_t *)paint)->sys_msg_colorbuffer;
            break;

        default:
            colorBuff = (uint16_t *)((paintHandle_t *)paint)->colorBuff;
            break;
    }

    uint16_t color = fill_color(buffer);

    switch (lcd_get_direction()) {
        case LCD_TRANSFORM_ROTATE_270: {
            for (int i = 0; i < (width); i++) {
                int pos_x = x + i;
                int pos_y = y + 0;
                int new_x = pos_y;
                int new_y = w - pos_x - 1;
                int index = new_y * (h) + new_x;

                for (int j = 0; j < (height); j++) {
                    colorBuff[index++] = color;
                }
            }

            break;
        }

        case LCD_TRANSFORM_ROTATE_90: {
            for (int i = 0; i < (width); i++) {
                int pos_x = x + i;
                int new_y = pos_x;
                int pos_y = y + 0;
                int new_x = h - pos_y - 1;
                int index = new_y *h + new_x;

                for (int j = 0; j < (height); j++) {
                    colorBuff[index--] = color;
                }
            }

            break;
        }

        case LCD_TRANSFORM_MIRROR_X: {
            for (int j = 0; j < (height); j++) {
                int pos_x = x + 0;
                int pos_y = y + j;
                int new_x = pos_x;
                int new_y = h - pos_y - 1;
                int index = new_y * (h) + new_x;

                for (int i = 0; i < (width); i++) {
                    colorBuff[index++] = color;
                }
            }

            break;
        }

        default: {
            for (int j = 0; j < (height); j++) {
                int pos_x = x + 0;
                int pos_y = y + j;
                int index = pos_y * (w) + pos_x;

                for (int i = 0; i < (width); i++) {
                    colorBuff[index++] = color;
                }
            }

            break;
        }
    }

    SDK_LOG_V("leave");
    return 0;
}

/*support user paint area , rgb565 fill buffer*/
OSI_SECTION_RW_KEEP int paint_draw_rect_with_cbuffer(int x, int y, uint32_t width, uint32_t height, void *cbuffer,
        const Paint_t paint)
{
    SDK_LOG_V("enter");
    lcd_hal_refresh_done();
    uint32_t h, w;
    lcd_get_size(&h, &w, NULL);
    uint16_t *colorBuff = (uint16_t *)((paintHandle_t *)paint)->colorBuff;
    uint16_t *fill_buffer = (uint16_t *)cbuffer;

    switch (lcd_get_direction()) {
        case LCD_TRANSFORM_ROTATE_270: {
            for (int i = 0; i < (width); i++) {
                int pos_x = x + i;
                int pos_y = y + 0;
                int new_x = pos_y;
                int new_y = w - pos_x - 1;
                int index = new_y * (h) + new_x;

                for (int j = 0; j < (height); j++) {
                    colorBuff[index++] = fill_color(fill_buffer[j *width + i]);
                }
            }

            break;
        }

        case LCD_TRANSFORM_ROTATE_90: {
            for (int i = 0; i < (width); i++) {
                int pos_x = x + i;
                int pos_y = y + 0;
                int new_x = h - pos_y - 1;
                int new_y = pos_x;
                int index = new_y * (h) + new_x;

                for (int j = 0; j < (height); j++) {
                    colorBuff[index--] = fill_color(fill_buffer[j *width + i]);
                }
            }

            break;
        }

        case LCD_TRANSFORM_MIRROR_X:
            break;

        default: {
            for (int j = 0; j < (height); j++) {
                for (int i = 0; i < (width); i++) {
                    int pos_x = x + i;
                    int pos_y = y + j;
                    colorBuff[pos_y * (w) + pos_x] = fill_color(fill_buffer[j *width + i]);
                }
            }

            break;
        }

    }

    SDK_LOG_V("leave");
    return 0;
}

OSI_SECTION_RW_KEEP static void draw_char(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *chr,
        uint8_t layer, const font_t pfont, const Paint_t paint)
{
    SDK_LOG_V("enter");
    uint16_t rowPixel = 0, row, col, size, index, bitIndex, temp_x, temp_y;
    uint8_t data;
    long fsIndex = 0, charLen;
    uint8_t *buf = NULL;
    uint32_t readLen = 0;

    if (pfont == NULL) {
        SDK_LOG_V("leave");
        return;
    }

    if (paint == NULL) {
        SDK_LOG_V("leave");
        return;
    }

    temp_x = 0;
    temp_y = 0;

    if (*chr < 0x80) {
        rowPixel = ((fontHandle_t *)pfont)->font_size >> 1;
    } else {
        rowPixel = ((fontHandle_t *)pfont)->font_size;
    }

    row = ((rowPixel >> 3) + ((rowPixel % 8) ? 1 : 0)); //row zise
    col = ((fontHandle_t *)pfont)->font_size; //col zise
    size = row *col; //byte

    if (*chr < 0x80) {
        fsIndex = ((*chr) * (size));
    } else {
        fsIndex = ((94 * (*chr - 0xa0 -1)) + (*(chr + 1) - 0xa0 -1)) * (size);
    }

    charLen = (size);
    buf = (uint8_t *)malloc(charLen);

    if (NULL == buf) {
        SDK_LOG_E("---buf is null");
        return;
    }

    SDK_LOG_V("row %d col %d size %d fsIndex %d", row, col, size, fsIndex);
    SDK_LOG_V("*chr 0x%x addr 0x%x", *chr, chr);

    if (*chr < 0x80) {
#ifdef USE_NOR_FLS
        /*read from nor flash*/
        read_font_from_norflash((unsigned int)buf, (unsigned int)fsIndex, (unsigned int)charLen, pfont);
        readLen = charLen;
#else
        if (((fontHandle_t *)pfont)->fd != 0) {
            fs_seek(((fontHandle_t *)pfont)->fd, fsIndex, SEEK_SET);
            readLen = fs_read(buf, 1, charLen, ((fontHandle_t *)pfont)->fd);
        }
        SDK_LOG_V("fd  %d fsIndex %d readLen %d charLen %d", ((fontHandle_t *)pfont)->fd, fsIndex, readLen, charLen);
#endif
    } else {
        if (((fontHandle_t *)pfont)->fd != 0) {
            fs_seek(((fontHandle_t *)pfont)->fd, fsIndex, SEEK_SET);
            readLen = fs_read(buf, 1, charLen, ((fontHandle_t *)pfont)->fd);
        }

        SDK_LOG_V("fd %d readLen %d  charLen %d", ((fontHandle_t *)pfont)->fd, readLen, charLen);
    }

    if (readLen != charLen) {
        SDK_LOG_E("charLen 0x%x readLen 0x%x fd %d second_fd %d", charLen, readLen, ((fontHandle_t *)pfont)->fd,
                  ((fontHandle_t *)pfont)->second_fd);
        free(buf);
        buf = NULL;
        return;
    }

    for (index = 0; index < charLen; index++) {
        data = buf[index];
        SDK_LOG_V("data 0x%02x", data);

        for (bitIndex = 0; bitIndex < 8; bitIndex++) {
            if ((data & (0x80 >> bitIndex)) != 0) { //draw valid point
                paint_draw_rect_paint(x + temp_x, y + temp_y, 1, 1, ((paintHandle_t *)paint)->fore_color, layer, paint);
            }

            temp_x += 1;

            if (temp_x >= row * 8) { //change line
                temp_y += 1;
                temp_x = 0;
                continue;
            }

            if (temp_x >= w) {
                SDK_LOG_V("reaching the maximum width. %d %d", x, temp_x);
                break;
            }

            if (temp_y >= h) {
                SDK_LOG_V("reaching the maximum height. %d %d", y, temp_y);
                break;
            }
        }
    }

    free(buf);
    buf = NULL;
    SDK_LOG_V("leave");
}
