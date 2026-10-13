#ifndef FRAMEWORK_LCD9853_PAINT_H
#define FRAMEWORK_LCD9853_PAINT_H

/**
 * @brief    此模块用于描述 <strong>字库(Font)模块</strong> 相关的接口
 * @author   liluchang
 * @date     2018-11-01
 * @version  V1.0
 * @remarks Change History:
 * Date          | Version       | Author          | Desc
 * ------------- | ------------- | --------------- | ----
 * 2019/01/24    | 1.0.0         | liluchang       | 创建文件
 */

/**
 * @defgroup FONT 字库
 * @{
 */
#include <stdint.h>
#include <string.h>
#include "stdlib.h"
typedef uint16_t WCHAR_t; /**< WCHAR_t类型定义   */
typedef void *font_t; /**< font指针类型定义  */

#if 0
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)                   (sizeof(x) / sizeof((x)[0]))
#endif

#define FONT_LIB_FLAG                   ".FONT."
#define PIXTABLE_FLAG                   "FONT HEAD INFO"

#define FONT_MAX_SUPPORT                64
#define FONT_LIB_MAX_SUPPORT            128
#endif

/**
 * @name 字体定义
 * @{
 */
#define FONT_COURIER_NEW                "CourierNew"        /**< CourierNew 字体    */
#define FONT_SONG                       "宋体"              /**< 宋体               */
#define FONT_IMITATE_SONG               "仿宋"              /**< 仿宋 字体          */
#define FONT_SYSTEM                     "System"            /**< System 字体        */
#define FONT_ARIAL                      "Arial"             /**< Arial 字体         */
#define FONT_TAHOMA                     "Tahoma"            /**< Tahoma 字体        */
#define FONT_VERDANA                    "Verdana"           /**< Verdana 字体       */
#define FONT_DOTUM                      "Dotum"             /**< Dotum 字体         */
/** @} */

/**
 * @name 字符集定义
 * @{
 */
#define FONT_CHARSET_ASCII              "ASCII"             /**< ASCII 字符集        */
#define FONT_CHARSET_GB2312             "GB2312"            /**< GB2312 字符集       */
#define FONT_CHARSET_GB18030            "GB18030"           /**< GB18030 字符集      */
#define FONT_CHARSET_VIETNAM            "VIETNAM"           /**< VIETNAM 字符集      */
#define FONT_CHARSET_ARABIC             "ARABIC"            /**< ARABIC 字符集       */
#define FONT_CHARSET_UNICODE            "UNICODE"           /**< UNICODE 字符集      */
#define FONT_CHARSET_CP437              "CP437"             /**< CP437 字符集        */
#define FONT_CHARSET_CP737              "CP737"             /**< CP737 字符集        */
#define FONT_CHARSET_CP775              "CP775"             /**< CP775 字符集        */
#define FONT_CHARSET_CP850              "CP850"             /**< CP850 字符集        */
#define FONT_CHARSET_CP852              "CP852"             /**< CP852 字符集        */
#define FONT_CHARSET_CP855              "CP855"             /**< CP855 字符集        */
#define FONT_CHARSET_CP857              "CP857"             /**< CP857 字符集        */
#define FONT_CHARSET_CP860              "CP860"             /**< CP860 字符集        */
#define FONT_CHARSET_CP861              "CP861"             /**< CP861 字符集        */
#define FONT_CHARSET_CP862              "CP862"             /**< CP862 字符集        */
#define FONT_CHARSET_CP863              "CP863"             /**< CP863 字符集        */
#define FONT_CHARSET_CP864              "CP864"             /**< CP864 字符集        */
#define FONT_CHARSET_CP865              "CP865"             /**< CP865 字符集        */
#define FONT_CHARSET_CP866              "CP866"             /**< CP866 字符集        */
#define FONT_CHARSET_CP869              "CP869"             /**< CP869 字符集        */
#define FONT_CHARSET_CP874              "CP874"             /**< CP874 字符集        */
#define FONT_CHARSET_CP932              "CP932"             /**< CP932 字符集        */
#define FONT_CHARSET_CP936              "CP936"             /**< CP936 字符集        */
#define FONT_CHARSET_CP949              "CP949"             /**< CP949 字符集        */
#define FONT_CHARSET_CP950              "CP950"             /**< CP950 字符集        */
#define FONT_CHARSET_CP1250             "CP1250"            /**< CP1250 字符集       */
#define FONT_CHARSET_CP1251             "CP1251"            /**< CP1251 字符集       */
#define FONT_CHARSET_CP1252             "CP1252"            /**< CP1252 字符集       */
#define FONT_CHARSET_CP1255             "CP1255"            /**< CP1255 字符集       */
#define FONT_CHARSET_ISO8859_1          "ISO8859-1"         /**< ISO8859-1 字符集    */
#define FONT_CHARSET_ISO8859_2          "ISO8859-2"         /**< ISO8859-2 字符集    */
#define FONT_CHARSET_ISO8859_3          "ISO8859-3"         /**< ISO8859-3 字符集    */
#define FONT_CHARSET_ISO8859_4          "ISO8859-4"         /**< ISO8859-4 字符集    */
#define FONT_CHARSET_ISO8859_5          "ISO8859-5"         /**< ISO8859-5 字符集    */
#define FONT_CHARSET_ISO8859_6          "ISO8859-6"         /**< ISO8859-6 字符集    */
#define FONT_CHARSET_ISO8859_7          "ISO8859-7"         /**< ISO8859-7 字符集    */
#define FONT_CHARSET_ISO8859_8          "ISO8859-8"         /**< ISO8859-8 字符集    */
#define FONT_CHARSET_ISO8859_9          "ISO8859-9"         /**< ISO8859-9 字符集    */
#define FONT_CHARSET_ISO8859_10         "ISO8859-10"        /**< ISO8859-10 字符集   */
#define FONT_CHARSET_ISO8859_11         "ISO8859-11"        /**< ISO8859-11 字符集   */
#define FONT_CHARSET_ISO8859_12         "ISO8859-12"        /**< ISO8859-12 字符集   */
#define FONT_CHARSET_ISO8859_13         "ISO8859-13"        /**< ISO8859-13 字符集   */
#define FONT_CHARSET_ISO8859_14         "ISO8859-14"        /**< ISO8859-14 字符集   */
#define FONT_CHARSET_ISO8859_15         "ISO8859-15"        /**< ISO8859-15 字符集   */
#define FONT_CHARSET_ISO8859_16         "ISO8859-16"        /**< ISO8859-16 字符集   */
/** @} */

//////////////////////////////////////////////////////////////////////////

//类型分类定义
#define SET_PRIMARY_FONT                0   /**< 设置首选字库 */
#define SET_SECONDARY_FONT              1   /**< 设置备选字库(针对首选字库为汉字，通过这个备选字库选择ascii部分的字体) */

//Farsi定义
#define TYPE_CONNECTOR                  0   /**< 具备4种字型状态且从右到左 */
#define TYPE_NONCONNECTOR               1   /**< 具备2种字型且从右到左 */
#define TYPE_RHYME                      2   /**< 具备与前一个字符结合的特性 */
#define TYPE_INTERPUNCTION              3   /**< 标点或者符号，独立成形 */
#define TYPE_INTER_NUM                  4   /**< 若没有连续出现且前后为数字则与数字结合 */
#define TYPE_CHAR                       5   /**< 字符类型，整体从左到右 */
#define TYPE_NUMBERIC                   6   /**< 数字类型，整体从左到右 */
#define TYPE_EXP                        7   /**< 指数 */
#define TYPE_NONE                       8   /**< 没有类型 */

//Vietnam定义
#define TYPE_NORMOL                     10  /**< 普通字符 */
#define TYPE_SUB                        11  /**< 下加符 */
#define TYPE_SUPER                      12  /**< 上加符 */

#define BigGlyphMetrics_size            8   /**<  BigGlyphMetrics size */
#define SmallGlyphMetrics_size          5   /**<  SmallGlyphMetrics size */
#define BigGlyphMetrics                 1   /**<  BigGlyphMetrics */
#define SmallGlyphMetrics               0   /**<  SmallGlyphMetrics */
#define CHR(ch1,ch2,ch3,ch4)            (((ch1)<<24)|((ch2)<<16)|((ch3)<<8)|(ch4)) /**< 汉字编码合成 */

/**
 * @brief  打开用户层的字库
 * @param[in]  fontname
 *          - 字库存放在文件系统中的绝对路径
 * @param[in]  charset
 *          - 字库使用的字符集，见字符集的宏定义
 * @param[in]  style
 *          - 字库的类型
 * @param[in]  size
 *          - 字体的大小
 * @param[out] ppfont
 *          - font指针的指针，用于输出字库打开后的操作指针句柄
 * @return int
 *          - 0 成功
 *          - <0 失败，返回相应的返回值
 */
int font_open_user(const char *fontname, const char *charset, int style, int size, font_t *ppfont);

/**
 * @brief  关闭用户层的字库
 * @param[in]  pfont
 *          - 打开字库的操作句柄
 * @return int
 *          - 0 成功
 *          - <0 失败，返回相应的返回值
 */
int font_close_user(const font_t pfont);

/**
 * @brief  获取字符(ASCII字符或汉字)在指定字库下的宽度和高度
 * @param[in]  pfont
 *          - 打开字库的操作句柄
 * @param[in]  lpText
 *          - 传入的字符的起始地址，可以是单个ASCII字符或一个汉字
 * @param[out]  width
 *          - 返回字符的宽度值
 * @param[out]  height
 *          - 返回字符的高度值
 * @return int
 *          - 0 成功
 *          - <0 失败，返回相应的返回值
 */
int font_get_word_info_user(const font_t pfont, const char *lpText, int *width, int *height);

/**
 * @brief  获取字符串在指定字库下的宽度
 * @param[in]  pfont
 *          - 打开字库的操作句柄
 * @param[in]  lpText
 *          - 传入的字符串指针
 * @param[in]  len
 *          - 传入字符串的长度
 * @return int
 *          - >= 0 成功，返回对应的宽度值
 *          - < 0  失败，返回相应的返回值
 */
int font_get_width_user(const font_t pfont, const char *lpText, int len);

void *get_open_font(void);

//fs_bmp
#pragma pack(1)  // 1 byte alignment
//bmp file header 14Byte
typedef struct {
    uint16_t bfType; //‘BM’
    uint32_t bfSize; //file size
    uint16_t bfReserved1;//
    uint16_t bfReserved2;//
    uint32_t bfOffBits; //sizeof(BITMAPFILEHEADER_) 14
} BITMAPFILEHEADER_;

//bmp info header
typedef struct {
    uint32_t biSize; // sizeof(BITMAPINFOHEADER_) 18
    uint32_t biWidth;//22
    uint32_t biHeight;//26
    uint16_t biPlanes; //28
    uint16_t biBitCount; //DIB 1/ 4/ 8/ 24  30
    uint32_t biCompression; //0-BI_RGB 1-BI_RLE8 2-BI_RLE4  34
    uint32_t biSizeImage; //bfSize-bfOffBits   38
    uint32_t biXPelsPerMeter; //x resolution   42
    uint32_t biYPelsPerMeter; //y resolution   46
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BITMAPINFOHEADER_;

//color
typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
} RGBQUAD_;

//bmp info header + color
typedef struct {
    BITMAPINFOHEADER_ bmiHeader;
    RGBQUAD_ bmiColors[1];
} BITMAPINFO_;

typedef struct {
    int fd_bmp;
    uint8_t reserved[8];
    BITMAPFILEHEADER_ *fileHeader;
    BITMAPINFOHEADER_ *infoHeader;
    RGBQUAD_ *colorTable;
    void *data;
} headerHandle_t;

#pragma pack()

/**
 * @description: open bmp handle
 * @param {char} *name: bmp file name
 * @param {void} **header: header pointer to a pointer, used to generate bmp file header handle
 * @return {*} 0:success -1:failed
 */
int open_bitmap(const char *name, void **header);

/**
 * @description: close bmp handle
 * @param {void} *header: point to bmp file header handle
 * @return {*} 0:success -1:failed
 */
int close_bitmap(void *header);

/**
 * @description: read bmp data region
 * @param {void} *data: pointer to data
 * @param {void} *header: point to bmp file header handle
 * @return {*} 0:success -1:failed
 */
int read_data(void *data, const void *header);

int read_color_table(void *data, const void *header);

int read_data_rect(void *data, void *rect, void *header);

//load data to internal malloc buffer
int open_bitmap_loadTobuffer(const char *name, void **header);

int close_bitmap_loadTobuffer(void *header);

int read_data_from_buffer(void *data, const void *header);

int read_color_table_from_buffer(void *data, const void *header);

/**
 * @brief  used for paint hzr logo; read bmp to internal buffer;
 * @param  *name: file name
 * @return color buffer addr
 */
void *read_bitmap_for_hzrlogo(const char *name);

/**
 * @brief  used for paint hzr logo; free internal buffer;
 * @return 0
 */
int free_bitmap_for_hzrlogo(void);


//paint
typedef void *Paint_t;

/**
 * @brief  该枚举列出了绘图换行模式.\n
 */
typedef enum {
    MODE_NO_WRAP = 0, /**< 非自动换行 */
    MODE_LINE_WRAP = 1, /**< 自动换行   */
} paint_newline_mode;

/**
 * @brief  该结构体指定了一个矩形框参数
 */
typedef struct _textRect {
    int m_x;/**< 矩形起点X坐标 */
    int m_y;/**< 矩形起点Y坐标  */
    uint32_t width; /**<高度  */
    uint32_t height; /**<  宽度 */
} textRect;

/**
 * @brief  创建用户层的画笔paint
 * @param[in]  width
 *          - 创建画布的像素宽度
 * @param[in]  height
 *          - 创建画布的像素高度
 * @param[in]  bits_per_pixel
 *          - 表示1像素使用的位数，即位/像素
 * @param[out]  paint
 *          - paint为指向指针的指针，用于输出打开paint的有效指针
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 */
int paint_create_user(int width, int height, int bits_per_pixel, Paint_t *paint);

/**
 * @brief  获取用户层的画笔paint的尺寸
 * @note   自定义接口
 * @param[out]  width
 *          - 创建画布的像素宽度
 * @param[out]  height
 *          - 创建画布的像素高度
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 */
int paint_get_size_user(uint32_t *width, uint32_t *height, const Paint_t paint);

/**
 * @brief  销毁用户层创建的画笔paint
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 */
int paint_destory_user(const Paint_t paint);

/**
 * @brief  在用户创建的画布上清除矩形区域
 * @param[in]  x
 *          - 起始位置的x坐标
 * @param[in]  y
 *          - 起始位置的y坐标
 * @param[in]  width
 *          - 矩形的宽度
 * @param[in]  height
 *          - 矩形的高度
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 */
int paint_clean_rect_user(int x, int y, uint32_t width, uint32_t height, const Paint_t paint);
int paint_clean_rect_user_with_layer(int x, int y, uint32_t width, uint32_t height,
                                     uint8_t layer, const Paint_t paint);

/**
 * @brief  在用户创建的画布上画图片
 * @param[in]  x
 *          - 起始位置的x坐标
 * @param[in]  y
 *          - 起始位置的y坐标
 * @param[in]  m_pic
 *          - BMP图片的完整数据内容，包含头部信息
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 * @note
 *          - 此接口暂时仅支持BMP图片，m_pic内容包括BMP图片的头部信息
 */
int paint_draw_picturefile_user(int x, int y, const void *m_pic, const Paint_t paint);

/**
 * @brief  在用户创建的画布上画字符串
 * @param[in]  pfont
 *          - 打开字库取的的有效字库操作句柄
 * @param[in]  x
 *          - 起始位置的x坐标，可传负值
 * @param[in]  y
 *          - 起始位置的y坐标
 * @param[in]  lpText
 *          - 字符串内容
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 * @note
 *          -当坐标是负值时，字体显示相当于超出显示屏区域，不会显示，可达到只显示一个字的部分效果。
 */
int paint_draw_text_user(font_t pfont, int x, int y, const char *lpText, const Paint_t paint);
int paint_draw_text_user_with_layer(font_t pfont, int x, int y, const char *lpText,
                                    uint8_t layer, const Paint_t paint);

/**
 * @brief  在用户创建的画布上画像素点
 * @param[in]  x
 *          - 画点的x坐标
 * @param[in]  y
 *          - 画点的y坐标
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 */
int paint_draw_pixel_user(int x, int y, const Paint_t paint);

/**
 * @brief  获取在用户创建的画布上画像素点的颜色值
 * @param[in]  x
 *          - 画点的x坐标
 * @param[in]  y
 *          - 画点的y坐标
 * @param[out]  color
 *          - 画点的颜色值，采用RGB888，24位色的表示形式
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 */
int paint_get_pixel_user(int x, int y, uint32_t *color, const Paint_t paint) ;

/**
 * @brief  设置用户层创建的画笔paint的前景色和背景色
 * @param[in]  back_color
 *          - 欲设置的背景颜色值，采用RGB888，24位色的表示形式
 * @param[in]  fore_color
 *          - 欲设置的前景颜色值，采用RGB888，24位色的表示形式
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - 0  成功
 *          - <0 失败，返回相应的返回值
 */
int paint_set_color_user(uint32_t back_color, uint32_t fore_color, Paint_t paint);

/**
 * @brief  获取用户层创建的画笔paint的前景色和背景色
 * @param[out]  back_color
 *          - 存储当前paint的背景颜色值，采用RGB888，24位色的表示形式
 * @param[out]  fore_color
 *          - 存储当前paint的前景颜色值，采用RGB888，24位色的表示形式
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - 0  成功
 *          - <0 失败，返回相应的返回值
 */
int paint_get_color_user(uint32_t *back_color, uint32_t *fore_color, const Paint_t paint);

/**
 * @brief  在用户创建的画布上画字符串，该画字符串区域被限制在一个参数有rect指定矩形框内
 * @param[in]  pfont
 *          - 打开字库取的的有效字库操作句柄
 * @param[in]  x
 *          - 起始位置的x坐标，可传负值
 * @param[in]  y
 *          - 起始位置的y坐标，可传负值
 * @param[in]  rect
 *          - 矩形框参数 参考@link textRect textRect @endlink
 * @param[in]  lpText
 *          - 字符串内容
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - =0  成功
 *          - <0  失败，返回相应的返回值
 * @note
 *          -注意该接口是限制在一个矩形框内显示，不会对矩形框外的区域打点，矩形框坐标只能在屏幕坐标范围内，当坐标传为（0，x），
 *           参数nX为负数时，可以显示在最左边，y坐标类似。
 */
int paint_draw_text_rect_user(void *fd, int nX, int nY, void *rect, const char *lpText, Paint_t paint);

/**
 * @brief  获取用户层创建的画笔paint的buffer内容
 * @param[in]  paint
 *          - paint create user返回的paint
 * @return int
 *          - 非NULL  成功，返回当前的paint绑定buffer内容
 *          - NULL    失败
 */
void *paint_get_buf_user(const Paint_t paint);

/**
 * @brief  释放用户层创建的画笔paint的buffer内容
 * @note   自定义接口
 * @param  paint:
 * @retval 0 success -1 fail
 */
int paint_free_buf_user(const Paint_t paint);

/*rgb565 fill buffer*/
/**
 * @brief  用定义的颜色表填充指定区域
 * @note   自定义接口
 * @param  x: 起始位置的x坐标
 * @param  y: 起始位置的y坐标
 * @param  width: 填充区域宽度
 * @param  height: 填充区域的高度
 * @param  *cbuffer: 定义的颜色表
 * @param  paint: paint create user返回的paint
 * @retval 0 success
 */
int paint_draw_rect_with_cbuffer(int x, int y, uint32_t width, uint32_t height, void *cbuffer, const Paint_t paint);

/**
 * @brief  打开bmp格式图片
 * @note   自定义接口
 * @param  *name: bmp图片名称，eg："D:/logo256.bmp"
 * @param  **bmpHandle: 打开bmp图片的有效指针
 * @retval 0 success
 * @retval -1 fail
 */
int paint_open_picture(const char *name, void **bmpHandle);

/**
 * @brief  关闭bmp图片
 * @note   自定义接口
 * @param  *bmpHandle: paint_open_picture打开的bmp图片的bmpHandle
 * @retval 0 success
 * @retval -1 fail
 */
int paint_close_picture(void *bmpHandle);

#endif