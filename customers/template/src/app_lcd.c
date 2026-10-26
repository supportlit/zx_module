#if 1


#include "app_lcd.h"
#include "at_api.h"
#include "audio_api.h"
#include "customer_config.h"
#include "customer_payment_handler.h"
#include "drv_camera.h"
#include "drv_def.h"
#include "drv_i2c.h"
#include "drv_iomux.h"
#include "drv_lcd.h"
#include "framework_device_config.h"
#include "gpio_api.h"
#include "lcd_api.h"
#include "osi_api.h"
#include "typedef.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CUSTOMER_ENABLE_LCD) && (CUSTOMER_ENABLE_LCD == 1)

#define PAYTM_NEW_LCD

// LCD
#define LCD_CS (3)
#define LCD_SDA (50)
#define LCD_SCL (49)

#define LCD_CS_PINMUX (GPIO_03)
#define LCD_SDA_PINMUX (GPIO_50)
#define LCD_SCL_PINMUX (GPIO_49)

#define CS(n) GpioSetLevel(LCD_CS, (n))
#define SDA(n) GpioSetLevel(LCD_SDA, (n))
#define SCL(n) GpioSetLevel(LCD_SCL, (n))
// #define nop()   for(int w = 0; w<5;w++) for(int q = 0; q<0xfffffff;q++)
// #define nop() osiThreadSleep(1)

#define COM_ROW(X, Y) ((((X) & 0xff) << 8) | ((Y) & 0xff))
#define COM(X) (((X) >> 8) & 0xff)
#define ROW(Y) ((Y) & 0xff)

osiMutex_t *lcdFlagMux = NULL;

static void nop(void) {
  volatile long a = 0, b = 0;
  volatile int q, w;
  for (w = 0; w < 0xF; w++) {
    for (q = 0; q < 0x4; q++) {
      a++;
      b++;
      a += b;
    }
  }
}

static void prvLcdGpioInit(void) {
  unsigned int mfp_lcd[] = {
      MFP_REG(LCD_CS_PINMUX) | MFP_AF0 | MFP_DRIVE_SLOW | MFP_PULL_NONE |
          MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
      MFP_REG(LCD_SCL_PINMUX) | MFP_AF0 | MFP_DRIVE_SLOW | MFP_PULL_NONE |
          MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
      MFP_REG(LCD_SDA_PINMUX) | MFP_AF0 | MFP_DRIVE_SLOW | MFP_PULL_NONE |
          MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
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

  GpioInitConfiguration(LCD_CS, cfg);
  GpioInitConfiguration(LCD_SCL, cfg);
  GpioInitConfiguration(LCD_SDA, cfg);
}

static char disp_buf[18] = {0};

#ifdef PAYTM_NEW_LCD
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
    0x6F  // 9
};

static int Decimal = 0;
static uint8_t front_fill = 0;

static void prvLcdPointSet(int levl) {
  int row_idx = 0;
  int com_idx = 0;
  short point_flag_map[] = {COM_ROW(3, 7), COM_ROW(3, 6), COM_ROW(3, 5),
                            COM_ROW(3, 4), COM_ROW(3, 3), COM_ROW(3, 2),
                            COM_ROW(3, 1), COM_ROW(3, 0)};

  osiMutexLock(lcdFlagMux);
  // clear
  for (int i = 0; i < sizeof(point_flag_map) / sizeof(point_flag_map[0]); i++) {
    row_idx = ROW(point_flag_map[i]);
    com_idx = COM(point_flag_map[i]);
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }

  if (levl == 0) {
    osiMutexUnlock(lcdFlagMux);
    return;
  }

  if (levl <= 8) {
    row_idx = ROW(point_flag_map[levl - 1]);
    com_idx = COM(point_flag_map[levl - 1]);
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    for (int i = 0; i < sizeof(point_flag_map) / sizeof(point_flag_map[0]);
         i++) {
      row_idx = ROW(point_flag_map[i]);
      com_idx = COM(point_flag_map[i]);
      {
        disp_buf[row_idx] |= (0x01 << com_idx);
      }
    }
  }
  osiMutexUnlock(lcdFlagMux);
}

static void prvLcdDecimalNumSet(int amount) {
  if (amount >= 0 && amount < 100) {
    Decimal = amount;
  }
}

static void prvLcdFrontFillNumSet(int amount) {
  if (amount >= 0 && amount < 8) {
    front_fill = amount;
  }
}

OSI_UNUSED static void prvLcdBigNumSet(const long amount) {
  int row_idx = 0;
  int com_idx = 0;
  uint8_t filllen = 0;
  char str[12] = {0};
  char str_de[4] = {0};

  short recv_data_map[8][7] = {
      // "A"          "B"              "C"             "D"             "E" "F"
      // "G"
      {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7),
       COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},
      {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6),
       COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
      {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5),
       COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
      {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4),
       COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
      {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3),
       COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
      {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2),
       COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
      {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1),
       COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
      {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0),
       COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
  };

  osiMutexLock(lcdFlagMux);
  // clear dig data
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }

  int data = amount;
  if (data < 0 && Decimal <= 0) {
    osiMutexUnlock(lcdFlagMux);
    return;
  }
  // baice
  // disp_buf[13] |= (0x01 << 2);
  // disp_buf[10] |= (0x01 << 0);
  if (front_fill >= 0) {
    filllen = front_fill;
  }
  if (filllen > 0) {
    sprintf(str, "%0*d%d", filllen, 0, data);
  } else {
    sprintf(str, "%d", data);
  }
  // RTI_LOG("str:%s",str);

  int memoy_len = strlen(str);
  if (memoy_len > 8) {
    memoy_len = 8;
  }
  for (int i = 0; i < memoy_len; i++) {
    // int num = data % 10;
    int dig = seg_map[str[i] - '0'] & 0xff;
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);

      if (dig & 0x01) {
        disp_buf[row_idx] |= (0x01 << com_idx);
      } else {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      }
      dig = dig >> 1;
    }
  }
  osiMutexUnlock(lcdFlagMux);
  if (Decimal > 0) {
    prvLcdPointSet(memoy_len);
    if (Decimal < 10) {
      sprintf(str_de, "%02d", Decimal);
    } else {
      sprintf(str_de, "%d", Decimal);
    }
    int len = memoy_len + 2;
    if (len > 8) {
      len = 8;
    }
    osiMutexLock(lcdFlagMux);
    for (int i = memoy_len; i < len; i++) {
      int dig = seg_map[str_de[i - memoy_len] - '0'] & 0xff;
      for (int j = 0; j < 7; j++) {
        row_idx = ROW(recv_data_map[i][j]);
        com_idx = COM(recv_data_map[i][j]);

        if (dig & 0x01) {
          disp_buf[row_idx] |= (0x01 << com_idx);
        } else {
          disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
        dig = dig >> 1;
      }
    }
    osiMutexUnlock(lcdFlagMux);
  }
}

void prvLcdSecCodeSet(const long amount) {
  int row_idx = 0;
  int com_idx = 0;
  char str[12] = {0};

  short recv_data_map[8][7] = {
      // "A"          "B"              "C"             "D"             "E" "F"
      // "G"
      {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7),
       COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},
      {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6),
       COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
      {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5),
       COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
      {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4),
       COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
      {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3),
       COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
      {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2),
       COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
      {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1),
       COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
      {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0),
       COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
  };

  osiMutexLock(lcdFlagMux);
  // clear dig data
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }

  int data = amount;
  if (data <= 0) {
    osiMutexUnlock(lcdFlagMux);
    return;
  }
  // baice
  disp_buf[13] |= (0x01 << 2);
  disp_buf[10] |= (0x01 << 0);
  sprintf(str, "%d", data);
  int memoy_len = strlen(str);
  for (int i = 0; i < memoy_len; i++) {
    // int num = data % 10;
    int dig = seg_map[str[i] - '0'] & 0xff;
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);

      if (dig & 0x01) {
        disp_buf[row_idx] |= (0x01 << com_idx);
      } else {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      }
      dig = dig >> 1;
    }
  }

  short point_flag_map[] = {COM_ROW(3, 7), COM_ROW(3, 6), COM_ROW(3, 5),
                            COM_ROW(3, 4), COM_ROW(3, 3), COM_ROW(3, 2),
                            COM_ROW(3, 1), COM_ROW(3, 0)};

  // clear
  for (int i = 0; i < sizeof(point_flag_map) / sizeof(point_flag_map[0]); i++) {
    row_idx = ROW(point_flag_map[i]);
    com_idx = COM(point_flag_map[i]);
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }

  for (int i = 0; i < memoy_len; i++) {
    row_idx = ROW(point_flag_map[i]);
    com_idx = COM(point_flag_map[i]);
    disp_buf[row_idx] |= (0x01 << com_idx);
  }
  osiMutexUnlock(lcdFlagMux);
  appAllrefresh();
}

void appLcdShowTextLIT(void) {
  short recv_data_map[8][7] = {
      // "A"          "B"              "C"             "D"             "E" "F"
      // "G"
      {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7),
       COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},
      {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6),
       COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
      {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5),
       COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
      {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4),
       COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
      {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3),
       COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
      {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2),
       COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
      {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1),
       COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
      {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0),
       COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
  };
  uint8_t lit_patterns[3] = {0x38, 0x06, 0x78}; // 'L', 'I', 't'
  osiMutexLock(lcdFlagMux);
  // Turn off Paytm segment bit directly without re-locking mutex
  disp_buf[13] &= ~(0x01 << 3);
  for (int i = 0; i < 3; i++) {
    uint8_t dig = lit_patterns[i];
    for (int j = 0; j < 7; j++) {
      int row_idx = ROW(recv_data_map[i][j]);
      int com_idx = COM(recv_data_map[i][j]);
      if (dig & 0x01)
        disp_buf[row_idx] |= (0x01 << com_idx);
      else
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      dig >>= 1;
    }
  }
  osiMutexUnlock(lcdFlagMux);
}
OSI_UNUSED static void prvLcdBigNumClr(void) {
  int row_idx = 0;
  int com_idx = 0;

  short recv_data_map[8][7] = {
      // "A"          "B"              "C"             "D"             "E" "F"
      // "G"
      {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0),
       COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
      {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1),
       COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
      {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2),
       COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
      {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3),
       COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
      {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4),
       COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
      {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5),
       COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
      {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6),
       COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
      {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7),
       COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},

  };
  osiMutexLock(lcdFlagMux);
  // clear dig data
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }

  // disp_buf[13] &= ((0x01 << 2) ^ 0xff);
  // disp_buf[10] &= ((0x01 << 0) ^ 0xff);
  osiMutexUnlock(lcdFlagMux);
}

OSI_UNUSED static void prvLcdReceiveAmountSet(const int amount) {
  int row_idx = 0;
  int com_idx = 0;

  //  3位数字显示数组
  short recv_data_map[4][7] = {
      // "D"          "C"              "B"             "A"             "E" "G"
      // "F"
      {COM_ROW(4, 8), COM_ROW(1, 8), COM_ROW(3, 8), COM_ROW(7, 8),
       COM_ROW(6, 8), COM_ROW(5, 8), COM_ROW(2, 8)}, // 9A- 11G
      {COM_ROW(4, 9), COM_ROW(1, 9), COM_ROW(3, 9), COM_ROW(7, 9),
       COM_ROW(6, 9), COM_ROW(5, 9), COM_ROW(2, 9)}, // 9A- 11G
      {COM_ROW(4, 10), COM_ROW(1, 10), COM_ROW(3, 10), COM_ROW(7, 10),
       COM_ROW(6, 10), COM_ROW(5, 10), COM_ROW(2, 10)}, // 9A- 11G
      {COM_ROW(4, 11), COM_ROW(1, 11), COM_ROW(3, 11), COM_ROW(7, 11),
       COM_ROW(6, 11), COM_ROW(5, 11), COM_ROW(2, 11)}, // 9A- 11G

  };
  osiMutexLock(lcdFlagMux);
  // clear dig data
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }

  int data = amount;
  if (data < 0) {
    osiMutexUnlock(lcdFlagMux);
    return;
  }
  for (int i = 0; i < 4; i++) {
    int num = data % 10;
    int dig = seg_map[num] & 0xff;
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);

      if (dig & 0x01) {
        disp_buf[row_idx] |= (0x01 << com_idx);
      } else {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      }
      dig = dig >> 1;
    }
    data = data / 10;
    if (i < 3 && data == 0) {
      break;
    }
  }
  osiMutexUnlock(lcdFlagMux);
}

OSI_UNUSED static void prvLcdReceiveAmountClr(void) {
  int row_idx = 0;
  int com_idx = 0;

  //  3位数字显示数组
  short recv_data_map[4][7] = {
      // "D"          "C"              "B"             "A"             "E" "G"
      // "F"
      {COM_ROW(4, 8), COM_ROW(1, 8), COM_ROW(3, 8), COM_ROW(7, 8),
       COM_ROW(6, 8), COM_ROW(5, 8), COM_ROW(2, 8)}, // 9A- 11G
      {COM_ROW(4, 9), COM_ROW(1, 9), COM_ROW(3, 9), COM_ROW(7, 9),
       COM_ROW(6, 9), COM_ROW(5, 9), COM_ROW(2, 9)}, // 9A- 11G
      {COM_ROW(4, 10), COM_ROW(1, 10), COM_ROW(3, 10), COM_ROW(7, 10),
       COM_ROW(6, 10), COM_ROW(5, 10), COM_ROW(2, 10)}, // 9A- 11G
      {COM_ROW(4, 11), COM_ROW(1, 11), COM_ROW(3, 11), COM_ROW(7, 11),
       COM_ROW(6, 11), COM_ROW(5, 11), COM_ROW(2, 11)}, // 9A- 11G

  };
  osiMutexLock(lcdFlagMux);
  // clear dig data
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }
  osiMutexUnlock(lcdFlagMux);
}
#else
//  3位数字                0     1     2     3     4     5,    6     7     8 9
static char dig_seg[] = {0x5f, 0x06, 0x3d, 0x2f, 0x66, 0x6b,
                         0x7b, 0x0e, 0x7f, 0x6f, 0x00};
//  6位数字                      0       1      2      3        4        5, 6 7
//  8        9
static long dig_seg_inv[] = {0x8799, 0x4002, 0xa559, 0xa1d9, 0x22d0, 0xa3c9,
                             0xa7c9, 0x0191, 0xa7d9, 0xa3d9, 0x0000};

OSI_UNUSED static void prvLcdBigNumSet(const long amount) {
  int row_idx = 0;
  int com_idx = 0;

  // 6位数字显示数组 每位数字由16个lcd显示器组成，该16位lcd显示器顺序如下：B J P
  // E C K M D A H G N I L O F
  short recv_data_map[6][16] = {

      {COM_ROW(0, 17), COM_ROW(1, 17), COM_ROW(2, 17), COM_ROW(3, 17),
       COM_ROW(4, 17), COM_ROW(5, 17), COM_ROW(6, 17), COM_ROW(7, 17),
       COM_ROW(0, 16), COM_ROW(1, 16), COM_ROW(2, 16), COM_ROW(3, 16),
       COM_ROW(4, 16), COM_ROW(5, 16), COM_ROW(6, 16), COM_ROW(7, 16)},
      {COM_ROW(0, 15), COM_ROW(1, 15), COM_ROW(2, 15), COM_ROW(3, 15),
       COM_ROW(4, 15), COM_ROW(5, 15), COM_ROW(6, 15), COM_ROW(7, 15),
       COM_ROW(0, 14), COM_ROW(1, 14), COM_ROW(2, 14), COM_ROW(3, 14),
       COM_ROW(4, 14), COM_ROW(5, 14), COM_ROW(6, 14), COM_ROW(7, 14)},
      {COM_ROW(0, 13), COM_ROW(1, 13), COM_ROW(2, 13), COM_ROW(3, 13),
       COM_ROW(4, 13), COM_ROW(5, 13), COM_ROW(6, 13), COM_ROW(7, 13),
       COM_ROW(0, 12), COM_ROW(1, 12), COM_ROW(2, 12), COM_ROW(3, 12),
       COM_ROW(4, 12), COM_ROW(5, 12), COM_ROW(6, 12), COM_ROW(7, 12)},
      {COM_ROW(0, 11), COM_ROW(1, 11), COM_ROW(2, 11), COM_ROW(3, 11),
       COM_ROW(4, 11), COM_ROW(5, 11), COM_ROW(6, 11), COM_ROW(7, 11),
       COM_ROW(0, 10), COM_ROW(1, 10), COM_ROW(2, 10), COM_ROW(3, 10),
       COM_ROW(4, 10), COM_ROW(5, 10), COM_ROW(6, 10), COM_ROW(7, 10)},
      {COM_ROW(0, 9), COM_ROW(1, 9), COM_ROW(2, 9), COM_ROW(3, 9),
       COM_ROW(4, 9), COM_ROW(5, 9), COM_ROW(6, 9), COM_ROW(7, 9),
       COM_ROW(0, 8), COM_ROW(1, 8), COM_ROW(2, 8), COM_ROW(3, 8),
       COM_ROW(4, 8), COM_ROW(5, 8), COM_ROW(6, 8), COM_ROW(7, 8)},
      {COM_ROW(0, 7), COM_ROW(1, 7), COM_ROW(2, 7), COM_ROW(3, 7),
       COM_ROW(4, 7), COM_ROW(5, 7), COM_ROW(6, 7), COM_ROW(7, 7),
       COM_ROW(0, 6), COM_ROW(1, 6), COM_ROW(2, 6), COM_ROW(3, 6),
       COM_ROW(4, 6), COM_ROW(5, 6), COM_ROW(6, 6), COM_ROW(7, 6)},

  };

  // clear dig data
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 16; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }

  long data = amount;
  if (data < 0) {
    return;
  }
  // RTI_LOG("data = %d, amount =%d", data, amount);
  if (data < 1000) {
    for (int i = 3; i < 6; i++) {
      int num = data % 10;
      long dig = dig_seg_inv[num] & 0xffff;
      // RTI_LOG("dig = 0x%x", dig);
      for (int j = 0; j < 16; j++) {
        row_idx = ROW(recv_data_map[i][j]);
        com_idx = COM(recv_data_map[i][j]);

        if (dig & 0x01) {
          disp_buf[row_idx] |= (0x01 << com_idx);
        } else {
          disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
        dig = dig >> 1;
      }
      data = data / 10;
      if (i < 6 && data == 0) {
        break;
      }
    }
  } else if (data < 10000) {
    for (int i = 2; i < 6; i++) {
      int num = data % 10;
      long dig = dig_seg_inv[num] & 0xffff;
      // RTI_LOG("dig = 0x%x", dig);
      for (int j = 0; j < 16; j++) {
        row_idx = ROW(recv_data_map[i][j]);
        com_idx = COM(recv_data_map[i][j]);

        if (dig & 0x01) {
          disp_buf[row_idx] |= (0x01 << com_idx);
        } else {
          disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
        dig = dig >> 1;
      }
      data = data / 10;
      if (i < 6 && data == 0) {
        break;
      }
    }
  } else if (data < 100000) {
    for (int i = 1; i < 6; i++) {
      int num = data % 10;
      long dig = dig_seg_inv[num] & 0xffff;
      // RTI_LOG("dig = 0x%x", dig);
      for (int j = 0; j < 16; j++) {
        row_idx = ROW(recv_data_map[i][j]);
        com_idx = COM(recv_data_map[i][j]);

        if (dig & 0x01) {
          disp_buf[row_idx] |= (0x01 << com_idx);
        } else {
          disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
        dig = dig >> 1;
      }
      data = data / 10;
      if (i < 6 && data == 0) {
        break;
      }
    }
  } else {
    for (int i = 0; i < 6; i++) {
      int num = data % 10;
      long dig = dig_seg_inv[num] & 0xffff;
      // RTI_LOG("dig = 0x%x", dig);
      for (int j = 0; j < 16; j++) {
        row_idx = ROW(recv_data_map[i][j]);
        com_idx = COM(recv_data_map[i][j]);

        if (dig & 0x01) {
          disp_buf[row_idx] |= (0x01 << com_idx);
        } else {
          disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
        dig = dig >> 1;
      }
      data = data / 10;
      if (i < 6 && data == 0) {
        break;
      }
    }
  }
}

OSI_UNUSED static void prvLcdBigNumClr(void) {
  int row_idx = 0;
  int com_idx = 0;

  // 6位数字显示数组 每位数字由16个lcd显示器组成，该16位lcd显示器顺序如下：B J P
  // E C K M D A H G N I L O F
  short recv_data_map[6][16] = {

      {COM_ROW(0, 17), COM_ROW(1, 17), COM_ROW(2, 17), COM_ROW(3, 17),
       COM_ROW(4, 17), COM_ROW(5, 17), COM_ROW(6, 17), COM_ROW(7, 17),
       COM_ROW(0, 16), COM_ROW(1, 16), COM_ROW(2, 16), COM_ROW(3, 16),
       COM_ROW(4, 16), COM_ROW(5, 16), COM_ROW(6, 16), COM_ROW(7, 16)},
      {COM_ROW(0, 15), COM_ROW(1, 15), COM_ROW(2, 15), COM_ROW(3, 15),
       COM_ROW(4, 15), COM_ROW(5, 15), COM_ROW(6, 15), COM_ROW(7, 15),
       COM_ROW(0, 14), COM_ROW(1, 14), COM_ROW(2, 14), COM_ROW(3, 14),
       COM_ROW(4, 14), COM_ROW(5, 14), COM_ROW(6, 14), COM_ROW(7, 14)},
      {COM_ROW(0, 13), COM_ROW(1, 13), COM_ROW(2, 13), COM_ROW(3, 13),
       COM_ROW(4, 13), COM_ROW(5, 13), COM_ROW(6, 13), COM_ROW(7, 13),
       COM_ROW(0, 12), COM_ROW(1, 12), COM_ROW(2, 12), COM_ROW(3, 12),
       COM_ROW(4, 12), COM_ROW(5, 12), COM_ROW(6, 12), COM_ROW(7, 12)},
      {COM_ROW(0, 11), COM_ROW(1, 11), COM_ROW(2, 11), COM_ROW(3, 11),
       COM_ROW(4, 11), COM_ROW(5, 11), COM_ROW(6, 11), COM_ROW(7, 11),
       COM_ROW(0, 10), COM_ROW(1, 10), COM_ROW(2, 10), COM_ROW(3, 10),
       COM_ROW(4, 10), COM_ROW(5, 10), COM_ROW(6, 10), COM_ROW(7, 10)},
      {COM_ROW(0, 9), COM_ROW(1, 9), COM_ROW(2, 9), COM_ROW(3, 9),
       COM_ROW(4, 9), COM_ROW(5, 9), COM_ROW(6, 9), COM_ROW(7, 9),
       COM_ROW(0, 8), COM_ROW(1, 8), COM_ROW(2, 8), COM_ROW(3, 8),
       COM_ROW(4, 8), COM_ROW(5, 8), COM_ROW(6, 8), COM_ROW(7, 8)},
      {COM_ROW(0, 7), COM_ROW(1, 7), COM_ROW(2, 7), COM_ROW(3, 7),
       COM_ROW(4, 7), COM_ROW(5, 7), COM_ROW(6, 7), COM_ROW(7, 7),
       COM_ROW(0, 6), COM_ROW(1, 6), COM_ROW(2, 6), COM_ROW(3, 6),
       COM_ROW(4, 6), COM_ROW(5, 6), COM_ROW(6, 6), COM_ROW(7, 6)},

  };

  // clear dig data
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 16; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }
}

OSI_UNUSED static void prvLcdReceiveAmountSet(const int amount) {
  int row_idx = 0;
  int com_idx = 0;

  //  3位数字显示数组
  short recv_data_map[3][7] = {
      // "D"          "C"              "B"             "A"             "E" "G"
      // "F"
      {COM_ROW(0, 0), COM_ROW(1, 0), COM_ROW(2, 0), COM_ROW(3, 0),
       COM_ROW(4, 0), COM_ROW(5, 0), COM_ROW(6, 0)}, // 9A- 11G
      {COM_ROW(0, 1), COM_ROW(1, 1), COM_ROW(2, 1), COM_ROW(3, 1),
       COM_ROW(4, 1), COM_ROW(5, 1), COM_ROW(6, 1)}, // 8A- 12G
      {COM_ROW(0, 2), COM_ROW(1, 2), COM_ROW(2, 2), COM_ROW(3, 2),
       COM_ROW(4, 2), COM_ROW(5, 2), COM_ROW(6, 2)}, // 7A- 13G

  };

  // clear dig data
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }

  int data = amount;
  if (data < 0) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    int num = data % 10;
    int dig = dig_seg[num] & 0xff;
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);

      if (dig & 0x01) {
        disp_buf[row_idx] |= (0x01 << com_idx);
      } else {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      }
      dig = dig >> 1;
    }
    data = data / 10;
    if (i < 2 && data == 0) {
      break;
    }
  }
}

OSI_UNUSED static void prvLcdReceiveAmountClr(void) {
  int row_idx = 0;
  int com_idx = 0;

  //  3位数字显示数组
  short recv_data_map[3][7] = {
      // "D"          "C"              "B"             "A"             "E" "G"
      // "F"
      {COM_ROW(0, 0), COM_ROW(1, 0), COM_ROW(2, 0), COM_ROW(3, 0),
       COM_ROW(4, 0), COM_ROW(5, 0), COM_ROW(6, 0)}, // 9A- 11G
      {COM_ROW(0, 1), COM_ROW(1, 1), COM_ROW(2, 1), COM_ROW(3, 1),
       COM_ROW(4, 1), COM_ROW(5, 1), COM_ROW(6, 1)}, // 8A- 12G
      {COM_ROW(0, 2), COM_ROW(1, 2), COM_ROW(2, 2), COM_ROW(3, 2),
       COM_ROW(4, 2), COM_ROW(5, 2), COM_ROW(6, 2)}, // 7A- 13G

  };

  // clear dig data
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 7; j++) {
      row_idx = ROW(recv_data_map[i][j]);
      com_idx = COM(recv_data_map[i][j]);
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }
}
#endif

static void Paytm_LCD_Set_RS_En(bool en) {
  short rs_flag_map = COM_ROW(0, 10);
  int row_idx = ROW(rs_flag_map);
  int com_idx = COM(rs_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Hook_En(bool en) {
  short hook_flag_map = COM_ROW(2, 13);
  int row_idx = ROW(hook_flag_map);
  int com_idx = COM(hook_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Count_En(bool en) {
#ifdef PAYTM_NEW_LCD
  short count_flag_map = COM_ROW(0, 11);
#else
  short count_flag_map = COM_ROW(7, 0);
#endif
  int row_idx = ROW(count_flag_map);
  int com_idx = COM(count_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_PayTm_En(bool en) {
#ifdef PAYTM_NEW_LCD
  short paytm_flag_map = COM_ROW(3, 13);
#else
  short paytm_flag_map = COM_ROW(7, 1);
#endif
  int row_idx = ROW(paytm_flag_map);
  int com_idx = COM(paytm_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

#ifndef PAYTM_NEW_LCD
static void Paytm_LCD_Set_BT_En(bool en) {
  short bt_flag_map = COM_ROW(5, 4);
  int row_idx = ROW(bt_flag_map);
  int com_idx = COM(bt_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Fm_En(bool en) {
  short fm_flag_map = COM_ROW(4, 4);
  int row_idx = ROW(fm_flag_map);
  int com_idx = COM(fm_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Wifi_Levl(int levl) {
  int row_idx = 0;
  int com_idx = 0;
  short wifi_flag_map[] = {COM_ROW(3, 5), COM_ROW(2, 5), COM_ROW(1, 5),
                           COM_ROW(0, 5)};
  if (levl > 4) {
    levl = 4;
  }
  osiMutexLock(lcdFlagMux);
  // clear
  for (int i = 0; i < sizeof(wifi_flag_map) / sizeof(wifi_flag_map[0]); i++) {
    row_idx = ROW(wifi_flag_map[i]);
    com_idx = COM(wifi_flag_map[i]);
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }

  // if (levl == 0)
  // {
  //     return;
  // }

  // set
  for (int i = 0; i < sizeof(wifi_flag_map) / sizeof(wifi_flag_map[0]); i++) {
    row_idx = ROW(wifi_flag_map[i]);
    com_idx = COM(wifi_flag_map[i]);
    if (i > levl) {
      osiMutexUnlock(lcdFlagMux);
      return;
    } else {
      disp_buf[row_idx] |= (0x01 << com_idx);
    }
  }
  osiMutexUnlock(lcdFlagMux);
}
#endif

void Paytm_LCD_Set_Volume_Levl(int levl) {
  int row_idx = 0;
  int com_idx = 0;
#ifdef PAYTM_NEW_LCD
  short signal_flag_map[] = {COM_ROW(4, 13), COM_ROW(5, 13), COM_ROW(6, 13),
                             COM_ROW(7, 13), COM_ROW(3, 14)};
#else
  short signal_flag_map[] = {COM_ROW(4, 5), COM_ROW(5, 5), COM_ROW(6, 5),
                             COM_ROW(7, 5)};
#endif
  if (levl > 4) {
    levl = 4;
  }
  osiMutexLock(lcdFlagMux);
  // clear
  for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]);
       i++) {
    row_idx = ROW(signal_flag_map[i]);
    com_idx = COM(signal_flag_map[i]);
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }

  if (levl <= 0) {
    osiMutexUnlock(lcdFlagMux);
    return;
  }

  // set
  for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]);
       i++) {
    row_idx = ROW(signal_flag_map[i]);
    com_idx = COM(signal_flag_map[i]);
    if (i > levl) {
      osiMutexUnlock(lcdFlagMux);
      return;
    } else {
      disp_buf[row_idx] |= (0x01 << com_idx);
    }
  }
  osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_Mute(bool en) {
  short paytm_flag_map[] = {COM_ROW(0, 13), COM_ROW(1, 13)};
  int row_idx;
  int com_idx;
  osiMutexLock(lcdFlagMux);
  for (int i = 0; i < sizeof(paytm_flag_map) / sizeof(paytm_flag_map[0]); i++) {
    row_idx = ROW(paytm_flag_map[i]);
    com_idx = COM(paytm_flag_map[i]);
    if (en) {
      disp_buf[row_idx] |= (0x01 << com_idx);
    } else {
      disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
  }
  osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_Signal_Levl(int levl) {
  int row_idx = 0;
  int com_idx = 0;
#ifdef PAYTM_NEW_LCD
  short signal_flag_map[] = {COM_ROW(3, 12), COM_ROW(2, 12), COM_ROW(1, 12),
                             COM_ROW(0, 12), COM_ROW(7, 12)};
#else
  short signal_flag_map[] = {COM_ROW(0, 4), COM_ROW(1, 4), COM_ROW(2, 4),
                             COM_ROW(3, 4)};
#endif
  if (levl > 4) {
    levl = 4;
  }
  osiMutexLock(lcdFlagMux);
  // clear
  for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]);
       i++) {
    row_idx = ROW(signal_flag_map[i]);
    com_idx = COM(signal_flag_map[i]);
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }

  // if (levl == 0)
  // {
  //     osiMutexUnlock(lcdFlagMux);
  //     return;
  // }

  // clear
  for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]);
       i++) {
    row_idx = ROW(signal_flag_map[i]);
    com_idx = COM(signal_flag_map[i]);
#ifdef PAYTM_NEW_LCD
    if (i > levl)
#else
    if (i >= levl)
#endif
    {
      osiMutexUnlock(lcdFlagMux);
      return;
    } else {
      disp_buf[row_idx] |= (0x01 << com_idx);
    }
  }
  osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_Power_Levl(int levl) {
  int row_idx = 0;
  int com_idx = 0;
#ifdef PAYTM_NEW_LCD
  short power_flag_map[] = {COM_ROW(4, 14), COM_ROW(2, 14), COM_ROW(0, 14),
                            COM_ROW(1, 14), COM_ROW(5, 14)};
  if (levl > 4) {
    levl = 4;
  }
#else
  short power_flag_map[] = {COM_ROW(2, 3), COM_ROW(4, 3), COM_ROW(0, 3),
                            COM_ROW(1, 3), COM_ROW(5, 3), COM_ROW(6, 3)};
  if (levl > 5) {
    levl = 5;
  }
#endif
  osiMutexLock(lcdFlagMux);
  // clear
  for (int i = 0; i < sizeof(power_flag_map) / sizeof(power_flag_map[0]); i++) {
    row_idx = ROW(power_flag_map[i]);
    com_idx = COM(power_flag_map[i]);
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  // #ifdef PAYTM_NEW_LCD
  //     if(levl == 0){
  //         osiMutexUnlock(lcdFlagMux);
  //         return;
  //     }
  // #endif
  // set
  for (int i = 0; i < sizeof(power_flag_map) / sizeof(power_flag_map[0]); i++) {
    row_idx = ROW(power_flag_map[i]);
    com_idx = COM(power_flag_map[i]);
    if (i > levl) {
      osiMutexUnlock(lcdFlagMux);
      return;
    } else {
      disp_buf[row_idx] |= (0x01 << com_idx);
    }
  }
  osiMutexUnlock(lcdFlagMux);
}
#ifdef PAYTM_NEW_LCD
void Paytm_LCD_Set_NoSignal_En(bool en) {
  short fm_flag_map = COM_ROW(6, 12);
  int row_idx = ROW(fm_flag_map);
  int com_idx = COM(fm_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_NoSim_En(bool en) {
  short fm_flag_map = COM_ROW(5, 12);
  int row_idx = ROW(fm_flag_map);
  int com_idx = COM(fm_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_NoConnect_En(bool en) {
  short fm_flag_map = COM_ROW(4, 12);
  int row_idx = ROW(fm_flag_map);
  int com_idx = COM(fm_flag_map);
  osiMutexLock(lcdFlagMux);
  if (en) {
    disp_buf[row_idx] |= (0x01 << com_idx);
  } else {
    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
  }
  osiMutexUnlock(lcdFlagMux);
}
#endif
static void prvLcdStart(void) {
  SDA(1);
  nop();
  SCL(1);
  nop();
  SDA(1);
  nop();
  SDA(0);
  nop();
}
static void prvLcdStop(void) {
  SCL(1);
  nop();
  SDA(0);
  nop();
  SDA(1);
  nop();
  SCL(0);
  nop();
  SDA(0);
}

static void prvLcdCmdAck(void) {
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
  while (GpioGetLevel(LCD_SDA)) {
    GpioInitConfiguration(LCD_SDA, cfg);
    count++;
    nop();
    if (count > 100) {
      RTI_LOG("ACK TIME OUT ");
      break;
    }
  }
  SCL(1);
  nop();
  SCL(0);
  nop();
  GpioSetDirection(LCD_SDA, 1);
  SDA(0);
  nop();
}

static void prvLcdSCLByte(char SDA) {
  for (int i = 0; i < 8; i++) {
    SCL(0);
    nop();
    if (SDA & 0x01)
      SDA(1);
    else
      SDA(0);
    nop();
    SCL(1);
    nop();
    SDA >>= 1;
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
  GpioInitConfiguration(LCD_SDA, cfg);
}

/***************写命令函数**************/
static void SCL_cmd(unsigned char cmd) {
  prvLcdStart();
  prvLcdSCLByte(cmd);
  prvLcdCmdAck();
  prvLcdStop();

  nop();
}

static void dispDma(unsigned char address, char *data, unsigned int data_len) {
  unsigned char i;
  unsigned char tmp;

  prvLcdStart();
  prvLcdSCLByte(address); // 起始地址
  prvLcdCmdAck();
  for (i = 0; i < data_len; i++) {
    tmp = data[i];
    prvLcdSCLByte(tmp);
    prvLcdCmdAck();
  }

  prvLcdStop();

  SCL_cmd(0x27); // 发送显示控制命令：正常模式，LINE模式，1/3偏压，5V
}

/***************向显存写数据**************/
/***************TM1726初始化函数**************/
static void init_tm1726() { SCL_cmd(0x27); }

void appLcdset(lcdParam_t lcd_buf) {
#ifdef PAYTM_NEW_LCD
  bool newLed = true;
#else
  bool newLed = false;
#endif
  if (!newLed) {
    // Paytm_LCD_Set_BT_En(lcd_buf.lcd_bt);
    // Paytm_LCD_Set_Fm_En(lcd_buf.lcd_fm);
    // Paytm_LCD_Set_Wifi_Levl(lcd_buf.lcd_wifi);
  } else {
    // Paytm_LCD_Set_Mute(lcd_buf.lcd_mute);
    // Paytm_LCD_Set_NoSignal_En(lcd_buf.lcd_NoSignal);
    // Paytm_LCD_Set_NoSim_En(lcd_buf.lcd_NoSim);
    // Paytm_LCD_Set_NoConnect_En(lcd_buf.lcd_NoConnect);
    prvLcdDecimalNumSet(lcd_buf.lcd_decimalnum);
    prvLcdFrontFillNumSet(lcd_buf.lcd_fill);
  }
  // Paytm_LCD_Set_PayTm_En(lcd_buf.lcd_paytm);
  // Paytm_LCD_Set_Power_Levl(lcd_buf.lcd_power);
  // Paytm_LCD_Set_Signal_Levl(lcd_buf.lcd_signal);
  // Paytm_LCD_Set_Volume_Levl(lcd_buf.lcd_vol);
  Paytm_LCD_Set_Hook_En(lcd_buf.lcd_hook);
  Paytm_LCD_Set_RS_En(lcd_buf.lcd_rs);
  Paytm_LCD_Set_Count_En(lcd_buf.lcd_count);
  // add by vipin: only display count digits if count is enabled, else clear to prevent rogue '0'
  if (lcd_buf.lcd_count) {
    prvLcdReceiveAmountSet(lcd_buf.lcd_amount);
  } else {
    prvLcdReceiveAmountClr();
  }
  prvLcdBigNumSet(lcd_buf.lcd_num);
#ifdef PAYTM_NEW_LCD
  appAllrefresh();
#else
  dispDma(0xc0, disp_buf, 18); // 0xc0 为LCD SEG0的起始位置,写入数据
#endif
}

void appLcdDisplayEnableALL(bool enbale) {
  osiMutexLock(lcdFlagMux);
  if (enbale) {
    memset(disp_buf, 0xff, sizeof(disp_buf));
  } else {
    memset(disp_buf, 0, sizeof(disp_buf));
  }

#ifdef PAYTM_NEW_LCD
  dispDma(0xc0, disp_buf, 15); // 0xc0 为LCD SEG0的起始位置,写入数据
#else
  dispDma(0xc0, disp_buf, 18); // 0xc0 为LCD SEG0的起始位置,写入数据
#endif
  osiMutexUnlock(lcdFlagMux);
  // osiThreadSleep(500 / 5);
}
extern void drvIsinkEnable(bool enabled, char level);
extern void drvIsinkPwmEnable(bool enabled, char level, char duty);
extern void osiSetPMEnableSleep(bool enable);

void app_test_backlight(void)
{
RTI_LOG("[LCD] Enabling Stable Backlight...\r\n");
    // 1. Backlight on rehte hue PMIC sleep band karein taaki flicker na ho
    osiSetPMEnableSleep(false);
    // 2. Stable Current Level (Level 3, Duty 0x30 / ~40%)
    drvIsinkPwmEnable(true, 3, 0x30);
}
void appLcdenable(void) {
  // long num1 = 0;
  // int num2 = 0;
  app_test_backlight();
  init_tm1726(); // 初始化
  nop();
  nop();

  Paytm_LCD_Set_PayTm_En(0);
  Paytm_LCD_Set_Power_Levl(4);
  Paytm_LCD_Set_Signal_Levl(4);
  Paytm_LCD_Set_Volume_Levl(framework_device_config_get_volume());
#ifdef PAYTM_NEW_LCD
  dispDma(0xc0, disp_buf, 15); // 0xc0 为LCD SEG0的起始位置,写入数据
#else
  dispDma(0xc0, disp_buf, 18); // 0xc0 为LCD SEG0的起始位置,写入数据
#endif
}

void appLcdenablecharger(void) {
  // init_tm1726(); // 初始化
  nop();
  nop();

  Paytm_LCD_Set_Power_Levl(0);
#ifdef PAYTM_NEW_LCD
  dispDma(0xc0, disp_buf, 15); // 0xc0 为LCD SEG0的起始位置,写入数据
#else
  dispDma(0xc0, disp_buf, 18); // 0xc0 为LCD SEG0的起始位置,写入数据
#endif
}

int appLcdInit(void) {

  if (lcdFlagMux == NULL) {
    lcdFlagMux = osiMutexCreate();
  } else {
    return true;
  }

  prvLcdGpioInit();
  RTI_LOG("COME HERE LCD INIT ");

  return 0;
}

void appnumclear(bool Amount, bool num) {
  bool flag = false;

  if (true == Amount) {
    prvLcdReceiveAmountClr();
#ifdef PAYTM_NEW_LCD
    Paytm_LCD_Set_Count_En(false);
#endif
    flag = true;
  }
  if (true == num) {
    prvLcdBigNumClr();
#ifdef PAYTM_NEW_LCD
    prvLcdPointSet(0);
    Paytm_LCD_Set_RS_En(false);
    Paytm_LCD_Set_Hook_En(false);
#endif
    flag = true;
  }
  if (true == flag) {
    osiMutexLock(lcdFlagMux);
#ifdef PAYTM_NEW_LCD
    dispDma(0xc0, disp_buf, 15); // 0xc0 为LCD SEG0的起始位置,写入数据
#else
    dispDma(0xc0, disp_buf, 18); // 0xc0 为LCD SEG0的起始位置,写入数据
#endif
    osiMutexUnlock(lcdFlagMux);
  }
}

void appAllrefresh(void) {
  osiMutexLock(lcdFlagMux);
#ifdef PAYTM_NEW_LCD
  dispDma(0xc0, disp_buf, 15); // 0xc0 为LCD SEG0的起始位置,写入数据
#else
  dispDma(0xc0, disp_buf, 18); // 0xc0 为LCD SEG0的起始位置,写入数据
#endif
  osiMutexUnlock(lcdFlagMux);
}

void appdatarefresh(int addr, int len) {
  osiMutexLock(lcdFlagMux);
#ifdef PAYTM_NEW_LCD
  dispDma((0xc0 + addr), &disp_buf[addr],
          len); // 0xc0 为LCD SEG0的起始位置,写入数据
#else
  dispDma((0xc0 + addr), &disp_buf[addr],
          len); // 0xc0 为LCD SEG0的起始位置,写入数据
#endif
  osiMutexUnlock(lcdFlagMux);
}

// add by vipin: Show "HITACHI" on idle LCD screen and clear transaction icons
void appLcdShowTextHitachi(void) {
  if (lcdFlagMux == NULL) {
    return;
  }

  static const short recv_data_map[8][7] = {
      // "A"          "B"              "C"             "D"             "E" "F" "G"
      {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7),
       COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},
      {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6),
       COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
      {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5),
       COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
      {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4),
       COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
      {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3),
       COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
      {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2),
       COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
      {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1),
       COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
      {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0),
       COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
  };

  /* Patterns for "HITACHI" across all 8 LCD digits:
   * Digit 0: 'H' = 0x76 (B+C+E+F+G)
   * Digit 1: 'I' = 0x06 (B+C)
   * Digit 2: 'T' left half  = 0x07 (A+B+C: top bar + right stem: ───┐)
   * Digit 3: 'T' right half = 0x31 (A+E+F: top bar + left stem:  ┌───)
   * Digit 4: 'A' = 0x77 (A+B+C+E+F+G)
   * Digit 5: 'C' = 0x39 (A+D+E+F)
   * Digit 6: 'H' = 0x76 (B+C+E+F+G)
   * Digit 7: 'I' = 0x06 (B+C)
   */
  static const uint8_t hitachi_patterns[8] = {
      0x76, // 'H' (digit 0)
      0x06, // 'I' (digit 1)
      0x07, // 'T' left half  (───┐) (digit 2)
      0x01, // 'T' right half (┌───) (digit 3)
      0x77, // 'A' (digit 4)
      0x39, // 'C' (digit 5)
      0x76, // 'H' (digit 6)
      0x06  // 'I' (digit 7)
  };

  // Show day count and COUNT icon beside HITACHI if transactions exist
  uint32_t count = customer_payment_get_total_count();
  if (count > 0) {
    Paytm_LCD_Set_Count_En(true);
    prvLcdReceiveAmountSet(count);
  } else {
    Paytm_LCD_Set_Count_En(false);
    prvLcdReceiveAmountClr();
  }

  osiMutexLock(lcdFlagMux);

  // Clear Hook (row 13 bit 2) and Paytm (row 13 bit 3)
  disp_buf[13] &= ~((0x01 << 2) | (0x01 << 3));
  // Clear RS (row 10 bit 0)
  disp_buf[10] &= ~(0x01 << 0);

  // Clear all decimal points (COM 3 on rows 0-7)
  for (int r = 0; r < 8; r++) {
    disp_buf[r] &= ~(0x01 << 3);
  }

  // Draw "HITACHI" on all 8 digits (T spans digits 2 and 3: ───┐ ┌───)
  for (int i = 0; i < 8; i++) {
    uint8_t dig = hitachi_patterns[i];
    for (int j = 0; j < 7; j++) {
      int row_idx = ROW(recv_data_map[i][j]);
      int com_idx = COM(recv_data_map[i][j]);
      if (dig & 0x01) {
        disp_buf[row_idx] |= (0x01 << com_idx);
      } else {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      }
      dig >>= 1;
    }
  }

  osiMutexUnlock(lcdFlagMux);

  appAllrefresh();
}

// add by vipin: Show Time in HH.MM.SS format on the 8 digits (e.g. 12.45.30)
void appLcdShowTimeVal(int hour, int min, int sec) {
  if (lcdFlagMux == NULL) {
    return;
  }

  static const short recv_data_map[8][7] = {
      {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7), COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)}, // Digit 0
      {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6), COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)}, // Digit 1
      {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5), COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)}, // Digit 2
      {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4), COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)}, // Digit 3
      {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3), COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)}, // Digit 4
      {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2), COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)}, // Digit 5
      {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1), COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)}, // Digit 6
      {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0), COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)}, // Digit 7
  };

  static const uint8_t s_digits_7seg[10] = {
      0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
  };

  // Keep Day count badge and count digits visible on bottom row
  uint32_t count = customer_payment_get_total_count();
  if (count > 0) {
    Paytm_LCD_Set_Count_En(true);
    prvLcdReceiveAmountSet(count);
  } else {
    Paytm_LCD_Set_Count_En(false);
    prvLcdReceiveAmountClr();
  }

  // Normalize time values
  if (hour < 0) hour = 0; if (hour > 23) hour = 23;
  if (min < 0) min = 0;   if (min > 59) min = 59;
  if (sec < 0) sec = 0;   if (sec > 59) sec = 59;

  // Digits 1..6 symmetrically centered: [blank][H1][H0.][M1][M0.][S1][S0][blank]
  uint8_t patterns[8] = {
      0x00,                                 // Digit 0: Blank
      s_digits_7seg[(hour / 10) % 10],       // Digit 1: Hours tens
      s_digits_7seg[hour % 10],             // Digit 2: Hours units
      s_digits_7seg[(min / 10) % 10],        // Digit 3: Minutes tens
      s_digits_7seg[min % 10],              // Digit 4: Minutes units
      s_digits_7seg[(sec / 10) % 10],        // Digit 5: Seconds tens
      s_digits_7seg[sec % 10],              // Digit 6: Seconds units
      0x00                                  // Digit 7: Blank
  };

  osiMutexLock(lcdFlagMux);

  // Clear Hook (row 13 bit 2) and Paytm (row 13 bit 3)
  disp_buf[13] &= ~((0x01 << 2) | (0x01 << 3));
  // Clear RS (row 10 bit 0)
  disp_buf[10] &= ~(0x01 << 0);

  // Clear all decimal points (COM 3 on rows 0-7)
  for (int r = 0; r < 8; r++) {
    disp_buf[r] &= ~(0x01 << 3);
  }

  // Set decimal points for colon simulation after Digit 2 and Digit 4:
  disp_buf[5] |= (0x01 << 3);
  disp_buf[3] |= (0x01 << 3);

  // Draw 7-segment patterns
  for (int i = 0; i < 8; i++) {
    uint8_t dig = patterns[i];
    for (int j = 0; j < 7; j++) {
      int row_idx = ROW(recv_data_map[i][j]);
      int com_idx = COM(recv_data_map[i][j]);
      if (dig & 0x01) {
        disp_buf[row_idx] |= (0x01 << com_idx);
      } else {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      }
      dig >>= 1;
    }
  }

  osiMutexUnlock(lcdFlagMux);
  appAllrefresh();
}

void appLcdShowTime(void) {
  osiTime_t now = {0};
  osiGetTime(&now);
  appLcdShowTimeVal(now.tm_hour, now.tm_min, now.tm_sec);
}

// add by vipin: Show Date in DD.MM.YY format on the 8 digits (e.g. 24.10.26)
void appLcdShowDateVal(int day, int mon, int year) {
  if (lcdFlagMux == NULL) {
    return;
  }

  static const short recv_data_map[8][7] = {
      {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7), COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},
      {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6), COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
      {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5), COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
      {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4), COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
      {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3), COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
      {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2), COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
      {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1), COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
      {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0), COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
  };

  static const uint8_t s_digits_7seg[10] = {
      0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
  };

  // Keep Day count badge and count digits visible on bottom row
  uint32_t count = customer_payment_get_total_count();
  if (count > 0) {
    Paytm_LCD_Set_Count_En(true);
    prvLcdReceiveAmountSet(count);
  } else {
    Paytm_LCD_Set_Count_En(false);
    prvLcdReceiveAmountClr();
  }

  // Normalize date values
  if (day < 1) day = 1;   if (day > 31) day = 31;
  if (mon < 1) mon = 1;   if (mon > 12) mon = 12;
  int yr_2digit = (year >= 0) ? (year % 100) : 0;

  // Digits 1..6 symmetrically centered: [blank][D1][D0.][M1][M0.][Y1][Y0][blank]
  uint8_t patterns[8] = {
      0x00,                                 // Digit 0: Blank
      s_digits_7seg[(day / 10) % 10],        // Digit 1: Day tens
      s_digits_7seg[day % 10],              // Digit 2: Day units
      s_digits_7seg[(mon / 10) % 10],        // Digit 3: Month tens
      s_digits_7seg[mon % 10],              // Digit 4: Month units
      s_digits_7seg[(yr_2digit / 10) % 10],  // Digit 5: Year tens
      s_digits_7seg[yr_2digit % 10],        // Digit 6: Year units
      0x00                                  // Digit 7: Blank
  };

  osiMutexLock(lcdFlagMux);

  // Clear Hook (row 13 bit 2) and Paytm (row 13 bit 3)
  disp_buf[13] &= ~((0x01 << 2) | (0x01 << 3));
  // Clear RS (row 10 bit 0)
  disp_buf[10] &= ~(0x01 << 0);

  // Clear all decimal points (COM 3 on rows 0-7)
  for (int r = 0; r < 8; r++) {
    disp_buf[r] &= ~(0x01 << 3);
  }

  // Set decimal points after Digit 2 and Digit 4:
  disp_buf[5] |= (0x01 << 3);
  disp_buf[3] |= (0x01 << 3);

  // Draw 7-segment patterns
  for (int i = 0; i < 8; i++) {
    uint8_t dig = patterns[i];
    for (int j = 0; j < 7; j++) {
      int row_idx = ROW(recv_data_map[i][j]);
      int com_idx = COM(recv_data_map[i][j]);
      if (dig & 0x01) {
        disp_buf[row_idx] |= (0x01 << com_idx);
      } else {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
      }
      dig >>= 1;
    }
  }

  osiMutexUnlock(lcdFlagMux);
  appAllrefresh();
}

void appLcdShowDate(void) {
  osiTime_t now = {0};
  osiGetTime(&now);
  appLcdShowDateVal(now.tm_mday, now.tm_mon, now.tm_year);
}

#endif /* CUSTOMER_ENABLE_LCD */


#else

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "typedef.h"
#include "drv_iomux.h"
#include "drv_def.h"
#include "drv_i2c.h"
#include "audio_api.h"
#include "gpio_api.h"
#include "osi_api.h"
#include "lcd_api.h"
#include "drv_camera.h"
#include "at_api.h"
#include "drv_lcd.h"
#include "app_lcd.h"
#include "customer_config.h"

#define PAYTM_NEW_LCD

// LCD
#define LCD_CS       (3)
#define LCD_SDA      (50)
#define LCD_SCL      (49) 

#define LCD_CS_PINMUX     (GPIO_03)
#define LCD_SDA_PINMUX    (GPIO_50)
#define LCD_SCL_PINMUX    (GPIO_49)

#define CS(n)  GpioSetLevel(LCD_CS, (n))
#define SDA(n) GpioSetLevel(LCD_SDA, (n))
#define SCL(n) GpioSetLevel(LCD_SCL, (n))
//#define nop()   for(int w = 0; w<5;w++) for(int q = 0; q<0xfffffff;q++)
//#define nop() osiThreadSleep(1)

#define COM_ROW(X, Y) ((((X) & 0xff) << 8) | ((Y) & 0xff))
#define COM(X) (((X) >> 8) & 0xff)
#define ROW(Y) ((Y) & 0xff)

osiMutex_t* lcdFlagMux = NULL;

static void nop(void)
{
    volatile  long a = 0, b = 0;
    volatile  int q, w;
    for (w = 0; w < 0xF; w++)
    {
        for (q = 0; q < 0x4; q++)
        {
            a++;
            b++;
            a += b;
        }
    }
}

static void prvLcdGpioInit(void)
{
    unsigned int mfp_lcd[] = {
        MFP_REG(LCD_CS_PINMUX) | MFP_AF0 | MFP_DRIVE_SLOW | MFP_PULL_NONE | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
        MFP_REG(LCD_SCL_PINMUX) | MFP_AF0 | MFP_DRIVE_SLOW | MFP_PULL_NONE | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
        MFP_REG(LCD_SDA_PINMUX) | MFP_AF0 | MFP_DRIVE_SLOW | MFP_PULL_NONE | MFP_SLEEP_NONE | MFP_LPM_EDGE_NONE,
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

    GpioInitConfiguration(LCD_CS, cfg);
    GpioInitConfiguration(LCD_SCL, cfg);
    GpioInitConfiguration(LCD_SDA, cfg);
}

static char disp_buf[18] = {0};

#ifdef PAYTM_NEW_LCD
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

static int Decimal = 0;
static uint8_t front_fill = 0;

static void prvLcdPointSet(int levl)
{
    int row_idx = 0;
    int com_idx = 0;
    short point_flag_map[] = {COM_ROW(3, 7), COM_ROW(3, 6), COM_ROW(3, 5), COM_ROW(3, 4), COM_ROW(3, 3), COM_ROW(3, 2), COM_ROW(3, 1), COM_ROW(3, 0)};

    osiMutexLock(lcdFlagMux);
    // clear
    for (int i = 0; i < sizeof(point_flag_map) / sizeof(point_flag_map[0]); i++)
    {
        row_idx = ROW(point_flag_map[i]);
        com_idx = COM(point_flag_map[i]);
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }

    if(levl == 0)
    {
        osiMutexUnlock(lcdFlagMux);
        return;
    }

    if(levl <= 8)
    {
        row_idx = ROW(point_flag_map[levl-1]);
        com_idx = COM(point_flag_map[levl-1]);
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        for (int i = 0; i < sizeof(point_flag_map) / sizeof(point_flag_map[0]); i++)
        {
            row_idx = ROW(point_flag_map[i]);
            com_idx = COM(point_flag_map[i]);
            {
                disp_buf[row_idx] |= (0x01 << com_idx);
            }
        }
    }
    osiMutexUnlock(lcdFlagMux);
}

static void prvLcdDecimalNumSet(int amount)
{
    if(amount >= 0 && amount < 100){
        Decimal = amount;
    }
}

static void prvLcdFrontFillNumSet(int amount)
{
    if(amount >= 0 && amount < 8){
        front_fill = amount;
    }
}

OSI_UNUSED static void prvLcdBigNumSet(const long amount)
{
    int row_idx = 0;
    int com_idx = 0;
    uint8_t filllen = 0;
    char str[12] = {0};
    char str_de[4] = {0};

    short recv_data_map[8][7] = {
        // "A"          "B"              "C"             "D"             "E"             "F"             "G"
        {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7), COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},
        {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6), COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
        {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5), COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
        {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4), COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
        {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3), COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
        {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2), COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
        {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1), COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
        {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0), COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
    };

    osiMutexLock(lcdFlagMux);
    // clear dig data
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }

    int data = amount;
    if(data < 0 && Decimal <= 0)
    {
        osiMutexUnlock(lcdFlagMux);
        return;
    }
    // baice
    // disp_buf[13] |= (0x01 << 2);
    // disp_buf[10] |= (0x01 << 0);
    if(front_fill >= 0){
        filllen = front_fill;
    }
    if(filllen > 0){
        sprintf(str, "%0*d%d", filllen,0,data);
    }else{
        sprintf(str, "%d", data);
    }
    // RTI_LOG("str:%s",str);

    int memoy_len = strlen(str);
    if(memoy_len > 8){memoy_len = 8;}
    for (int i = 0; i < memoy_len; i++)
    {
        // int num = data % 10;
        int dig = seg_map[str[i] - '0'] & 0xff;
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);

            if (dig & 0x01)
            {
                disp_buf[row_idx] |= (0x01 << com_idx);
            }
            else
            {
                disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
            }
            dig = dig >> 1;
        }
    }
    osiMutexUnlock(lcdFlagMux);
    if(Decimal > 0)
    {
        prvLcdPointSet(memoy_len);
        if(Decimal < 10){
            sprintf(str_de, "%02d", Decimal);
        }else{
            sprintf(str_de, "%d", Decimal);
        }
        int len = memoy_len+2;
        if(len > 8){len = 8;}
        osiMutexLock(lcdFlagMux);
        for (int i = memoy_len; i < len; i++)
        {
            int dig = seg_map[str_de[i-memoy_len] - '0'] & 0xff;
            for (int j = 0; j < 7; j++)
            {
                row_idx = ROW(recv_data_map[i][j]);
                com_idx = COM(recv_data_map[i][j]);

                if (dig & 0x01)
                {
                    disp_buf[row_idx] |= (0x01 << com_idx);
                }
                else
                {
                    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
                }
                dig = dig >> 1;
            }
        }
        osiMutexUnlock(lcdFlagMux);
    }
}

void prvLcdSecCodeSet(const long amount)
{
    int row_idx = 0;
    int com_idx = 0;
    char str[12] = {0};

    short recv_data_map[8][7] = {
        // "A"          "B"              "C"             "D"             "E"             "F"             "G"
        {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7), COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},
        {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6), COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
        {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5), COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
        {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4), COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
        {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3), COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
        {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2), COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
        {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1), COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
        {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0), COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
    };

    osiMutexLock(lcdFlagMux);
    // clear dig data
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }

    int data = amount;
    if(data <= 0)
    {
        osiMutexUnlock(lcdFlagMux);
        return;
    }
    // baice
    disp_buf[13] |= (0x01 << 2);
    disp_buf[10] |= (0x01 << 0);
    sprintf(str, "%d", data);
    int memoy_len = strlen(str);
    for (int i = 0; i < memoy_len; i++)
    {
        // int num = data % 10;
        int dig = seg_map[str[i] - '0'] & 0xff;
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);

            if (dig & 0x01)
            {
                disp_buf[row_idx] |= (0x01 << com_idx);
            }
            else
            {
                disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
            }
            dig = dig >> 1;
        }
    }

    short point_flag_map[] = {COM_ROW(3, 7), COM_ROW(3, 6), COM_ROW(3, 5), COM_ROW(3, 4), COM_ROW(3, 3), COM_ROW(3, 2), COM_ROW(3, 1), COM_ROW(3, 0)};

    // clear
    for (int i = 0; i < sizeof(point_flag_map) / sizeof(point_flag_map[0]); i++)
    {
        row_idx = ROW(point_flag_map[i]);
        com_idx = COM(point_flag_map[i]);
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }

    for (int i = 0; i < memoy_len; i++)
    {
        row_idx = ROW(point_flag_map[i]);
        com_idx = COM(point_flag_map[i]);
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    osiMutexUnlock(lcdFlagMux);

}
OSI_UNUSED static void prvLcdBigNumClr(void)
{
    int row_idx = 0;
    int com_idx = 0;

    short recv_data_map[8][7] = {
        // "A"          "B"              "C"             "D"             "E"             "F"             "G"
        {COM_ROW(4, 0), COM_ROW(0, 0), COM_ROW(2, 0), COM_ROW(7, 0), COM_ROW(6, 0), COM_ROW(5, 0), COM_ROW(1, 0)},
        {COM_ROW(4, 1), COM_ROW(0, 1), COM_ROW(2, 1), COM_ROW(7, 1), COM_ROW(6, 1), COM_ROW(5, 1), COM_ROW(1, 1)},
        {COM_ROW(4, 2), COM_ROW(0, 2), COM_ROW(2, 2), COM_ROW(7, 2), COM_ROW(6, 2), COM_ROW(5, 2), COM_ROW(1, 2)},
        {COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(2, 3), COM_ROW(7, 3), COM_ROW(6, 3), COM_ROW(5, 3), COM_ROW(1, 3)},
        {COM_ROW(4, 4), COM_ROW(0, 4), COM_ROW(2, 4), COM_ROW(7, 4), COM_ROW(6, 4), COM_ROW(5, 4), COM_ROW(1, 4)},
        {COM_ROW(4, 5), COM_ROW(0, 5), COM_ROW(2, 5), COM_ROW(7, 5), COM_ROW(6, 5), COM_ROW(5, 5), COM_ROW(1, 5)},
        {COM_ROW(4, 6), COM_ROW(0, 6), COM_ROW(2, 6), COM_ROW(7, 6), COM_ROW(6, 6), COM_ROW(5, 6), COM_ROW(1, 6)},
        {COM_ROW(4, 7), COM_ROW(0, 7), COM_ROW(2, 7), COM_ROW(7, 7), COM_ROW(6, 7), COM_ROW(5, 7), COM_ROW(1, 7)},

    };
    osiMutexLock(lcdFlagMux);
    // clear dig data
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }

    // disp_buf[13] &= ((0x01 << 2) ^ 0xff);
    // disp_buf[10] &= ((0x01 << 0) ^ 0xff);
    osiMutexUnlock(lcdFlagMux);

}

OSI_UNUSED static void prvLcdReceiveAmountSet(const int amount)
{
    int row_idx = 0;
    int com_idx = 0;

    //  3位数字显示数组
    short recv_data_map[4][7] = {
        // "D"          "C"              "B"             "A"             "E"             "G"             "F"
        {COM_ROW(4, 8), COM_ROW(1, 8), COM_ROW(3, 8), COM_ROW(7, 8), COM_ROW(6, 8), COM_ROW(5, 8), COM_ROW(2, 8)}, // 9A- 11G
        {COM_ROW(4, 9), COM_ROW(1, 9), COM_ROW(3, 9), COM_ROW(7, 9), COM_ROW(6, 9), COM_ROW(5, 9), COM_ROW(2, 9)}, // 9A- 11G
        {COM_ROW(4, 10), COM_ROW(1, 10), COM_ROW(3, 10), COM_ROW(7, 10), COM_ROW(6, 10), COM_ROW(5, 10), COM_ROW(2, 10)}, // 9A- 11G
        {COM_ROW(4, 11), COM_ROW(1, 11), COM_ROW(3, 11), COM_ROW(7, 11), COM_ROW(6, 11), COM_ROW(5, 11), COM_ROW(2, 11)}, // 9A- 11G

    };
    osiMutexLock(lcdFlagMux);
    // clear dig data
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }

    int data = amount;
    if(data < 0)
    {
        osiMutexUnlock(lcdFlagMux);
        return;
    }
    for (int i = 0; i < 4; i++)
    {
        int num = data % 10;
        int dig = seg_map[num] & 0xff;
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);

            if (dig & 0x01)
            {
                disp_buf[row_idx] |= (0x01 << com_idx);
            }
            else
            {
                disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
            }
            dig = dig >> 1;
        }
        data = data / 10;
        if (i < 3 && data == 0)
        {
            break;
        }
    }
    osiMutexUnlock(lcdFlagMux);
}

OSI_UNUSED static void prvLcdReceiveAmountClr(void)
{
    int row_idx = 0;
    int com_idx = 0;

    //  3位数字显示数组
    short recv_data_map[4][7] = {
        // "D"          "C"              "B"             "A"             "E"             "G"             "F"
        {COM_ROW(4, 8), COM_ROW(1, 8), COM_ROW(3, 8), COM_ROW(7, 8), COM_ROW(6, 8), COM_ROW(5, 8), COM_ROW(2, 8)}, // 9A- 11G
        {COM_ROW(4, 9), COM_ROW(1, 9), COM_ROW(3, 9), COM_ROW(7, 9), COM_ROW(6, 9), COM_ROW(5, 9), COM_ROW(2, 9)}, // 9A- 11G
        {COM_ROW(4, 10), COM_ROW(1, 10), COM_ROW(3, 10), COM_ROW(7, 10), COM_ROW(6, 10), COM_ROW(5, 10), COM_ROW(2, 10)}, // 9A- 11G
        {COM_ROW(4, 11), COM_ROW(1, 11), COM_ROW(3, 11), COM_ROW(7, 11), COM_ROW(6, 11), COM_ROW(5, 11), COM_ROW(2, 11)}, // 9A- 11G

    };
    osiMutexLock(lcdFlagMux);
    // clear dig data
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }
    osiMutexUnlock(lcdFlagMux);
}
#else
//  3位数字                0     1     2     3     4     5,    6     7     8     9
static char dig_seg[] = {0x5f, 0x06, 0x3d, 0x2f, 0x66, 0x6b, 0x7b, 0x0e, 0x7f, 0x6f, 0x00};
//  6位数字                      0       1      2      3        4        5,      6       7      8        9
static long dig_seg_inv[] = {0x8799, 0x4002, 0xa559, 0xa1d9, 0x22d0, 0xa3c9, 0xa7c9, 0x0191, 0xa7d9, 0xa3d9, 0x0000};

OSI_UNUSED static void prvLcdBigNumSet(const long amount)
{
    int row_idx = 0;
    int com_idx = 0;

    // 6位数字显示数组 每位数字由16个lcd显示器组成，该16位lcd显示器顺序如下：B J P E C K M D A H G N I L O F
    short recv_data_map[6][16] = {

        {COM_ROW(0, 17), COM_ROW(1, 17), COM_ROW(2, 17), COM_ROW(3, 17), COM_ROW(4, 17), COM_ROW(5, 17), COM_ROW(6, 17), COM_ROW(7, 17),
         COM_ROW(0, 16), COM_ROW(1, 16), COM_ROW(2, 16), COM_ROW(3, 16), COM_ROW(4, 16), COM_ROW(5, 16), COM_ROW(6, 16), COM_ROW(7, 16)},
        {COM_ROW(0, 15), COM_ROW(1, 15), COM_ROW(2, 15), COM_ROW(3, 15), COM_ROW(4, 15), COM_ROW(5, 15), COM_ROW(6, 15), COM_ROW(7, 15),
         COM_ROW(0, 14), COM_ROW(1, 14), COM_ROW(2, 14), COM_ROW(3, 14), COM_ROW(4, 14), COM_ROW(5, 14), COM_ROW(6, 14), COM_ROW(7, 14)},
        {COM_ROW(0, 13), COM_ROW(1, 13), COM_ROW(2, 13), COM_ROW(3, 13), COM_ROW(4, 13), COM_ROW(5, 13), COM_ROW(6, 13), COM_ROW(7, 13),
         COM_ROW(0, 12), COM_ROW(1, 12), COM_ROW(2, 12), COM_ROW(3, 12), COM_ROW(4, 12), COM_ROW(5, 12), COM_ROW(6, 12), COM_ROW(7, 12)},
        {COM_ROW(0, 11), COM_ROW(1, 11), COM_ROW(2, 11), COM_ROW(3, 11), COM_ROW(4, 11), COM_ROW(5, 11), COM_ROW(6, 11), COM_ROW(7, 11),
         COM_ROW(0, 10), COM_ROW(1, 10), COM_ROW(2, 10), COM_ROW(3, 10), COM_ROW(4, 10), COM_ROW(5, 10), COM_ROW(6, 10), COM_ROW(7, 10)},
        {COM_ROW(0, 9), COM_ROW(1, 9), COM_ROW(2, 9), COM_ROW(3, 9), COM_ROW(4, 9), COM_ROW(5, 9), COM_ROW(6, 9), COM_ROW(7, 9),
         COM_ROW(0, 8), COM_ROW(1, 8), COM_ROW(2, 8), COM_ROW(3, 8), COM_ROW(4, 8), COM_ROW(5, 8), COM_ROW(6, 8), COM_ROW(7, 8)},
        {COM_ROW(0, 7), COM_ROW(1, 7), COM_ROW(2, 7), COM_ROW(3, 7), COM_ROW(4, 7), COM_ROW(5, 7), COM_ROW(6, 7), COM_ROW(7, 7),
         COM_ROW(0, 6), COM_ROW(1, 6), COM_ROW(2, 6), COM_ROW(3, 6), COM_ROW(4, 6), COM_ROW(5, 6), COM_ROW(6, 6), COM_ROW(7, 6)},

    };

    // clear dig data
    for (int i = 0; i < 6; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }

    long data = amount;
    if(data < 0)
    {
        return;
    }
    //RTI_LOG("data = %d, amount =%d", data, amount);
    if(data < 1000)
    {
        for (int i = 3; i < 6; i++)
        {
            int num = data % 10;
            long dig = dig_seg_inv[num] & 0xffff;
            //RTI_LOG("dig = 0x%x", dig);
            for (int j = 0; j < 16; j++)
            {
                row_idx = ROW(recv_data_map[i][j]);
                com_idx = COM(recv_data_map[i][j]);

                if (dig & 0x01)
                {
                    disp_buf[row_idx] |= (0x01 << com_idx);
                }
                else
                {
                    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
                }
                dig = dig >> 1;
            }
            data = data / 10;
            if (i < 6 && data == 0)
            {
                break;
            }
        }
    }
    else if(data < 10000)
    {
        for (int i = 2; i < 6; i++)
        {
            int num = data % 10;
            long dig = dig_seg_inv[num] & 0xffff;
            //RTI_LOG("dig = 0x%x", dig);
            for (int j = 0; j < 16; j++)
            {
                row_idx = ROW(recv_data_map[i][j]);
                com_idx = COM(recv_data_map[i][j]);

                if (dig & 0x01)
                {
                    disp_buf[row_idx] |= (0x01 << com_idx);
                }
                else
                {
                    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
                }
                dig = dig >> 1;
            }
            data = data / 10;
            if (i < 6 && data == 0)
            {
                break;
            }
        }
    }
    else if(data < 100000)
    {
        for (int i = 1; i < 6; i++)
        {
            int num = data % 10;
            long dig = dig_seg_inv[num] & 0xffff;
            //RTI_LOG("dig = 0x%x", dig);
            for (int j = 0; j < 16; j++)
            {
                row_idx = ROW(recv_data_map[i][j]);
                com_idx = COM(recv_data_map[i][j]);

                if (dig & 0x01)
                {
                    disp_buf[row_idx] |= (0x01 << com_idx);
                }
                else
                {
                    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
                }
                dig = dig >> 1;
            }
            data = data / 10;
            if (i < 6 && data == 0)
            {
                break;
            }
        }
    }
    else
    {
        for (int i = 0; i < 6; i++)
        {
            int num = data % 10;
            long dig = dig_seg_inv[num] & 0xffff;
            //RTI_LOG("dig = 0x%x", dig);
            for (int j = 0; j < 16; j++)
            {
                row_idx = ROW(recv_data_map[i][j]);
                com_idx = COM(recv_data_map[i][j]);

                if (dig & 0x01)
                {
                    disp_buf[row_idx] |= (0x01 << com_idx);
                }
                else
                {
                    disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
                }
                dig = dig >> 1;
            }
            data = data / 10;
            if (i < 6 && data == 0)
            {
                break;
            }
        }
    }

}

OSI_UNUSED static void prvLcdBigNumClr(void)
{
    int row_idx = 0;
    int com_idx = 0;

    // 6位数字显示数组 每位数字由16个lcd显示器组成，该16位lcd显示器顺序如下：B J P E C K M D A H G N I L O F
    short recv_data_map[6][16] = {

        {COM_ROW(0, 17), COM_ROW(1, 17), COM_ROW(2, 17), COM_ROW(3, 17), COM_ROW(4, 17), COM_ROW(5, 17), COM_ROW(6, 17), COM_ROW(7, 17),
         COM_ROW(0, 16), COM_ROW(1, 16), COM_ROW(2, 16), COM_ROW(3, 16), COM_ROW(4, 16), COM_ROW(5, 16), COM_ROW(6, 16), COM_ROW(7, 16)},
        {COM_ROW(0, 15), COM_ROW(1, 15), COM_ROW(2, 15), COM_ROW(3, 15), COM_ROW(4, 15), COM_ROW(5, 15), COM_ROW(6, 15), COM_ROW(7, 15),
         COM_ROW(0, 14), COM_ROW(1, 14), COM_ROW(2, 14), COM_ROW(3, 14), COM_ROW(4, 14), COM_ROW(5, 14), COM_ROW(6, 14), COM_ROW(7, 14)},
        {COM_ROW(0, 13), COM_ROW(1, 13), COM_ROW(2, 13), COM_ROW(3, 13), COM_ROW(4, 13), COM_ROW(5, 13), COM_ROW(6, 13), COM_ROW(7, 13),
         COM_ROW(0, 12), COM_ROW(1, 12), COM_ROW(2, 12), COM_ROW(3, 12), COM_ROW(4, 12), COM_ROW(5, 12), COM_ROW(6, 12), COM_ROW(7, 12)},
        {COM_ROW(0, 11), COM_ROW(1, 11), COM_ROW(2, 11), COM_ROW(3, 11), COM_ROW(4, 11), COM_ROW(5, 11), COM_ROW(6, 11), COM_ROW(7, 11),
         COM_ROW(0, 10), COM_ROW(1, 10), COM_ROW(2, 10), COM_ROW(3, 10), COM_ROW(4, 10), COM_ROW(5, 10), COM_ROW(6, 10), COM_ROW(7, 10)},
        {COM_ROW(0, 9), COM_ROW(1, 9), COM_ROW(2, 9), COM_ROW(3, 9), COM_ROW(4, 9), COM_ROW(5, 9), COM_ROW(6, 9), COM_ROW(7, 9),
         COM_ROW(0, 8), COM_ROW(1, 8), COM_ROW(2, 8), COM_ROW(3, 8), COM_ROW(4, 8), COM_ROW(5, 8), COM_ROW(6, 8), COM_ROW(7, 8)},
        {COM_ROW(0, 7), COM_ROW(1, 7), COM_ROW(2, 7), COM_ROW(3, 7), COM_ROW(4, 7), COM_ROW(5, 7), COM_ROW(6, 7), COM_ROW(7, 7),
         COM_ROW(0, 6), COM_ROW(1, 6), COM_ROW(2, 6), COM_ROW(3, 6), COM_ROW(4, 6), COM_ROW(5, 6), COM_ROW(6, 6), COM_ROW(7, 6)},

    };

    // clear dig data
    for (int i = 0; i < 6; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }
}

OSI_UNUSED static void prvLcdReceiveAmountSet(const int amount)
{
    int row_idx = 0;
    int com_idx = 0;

    //  3位数字显示数组
    short recv_data_map[3][7] = {
        // "D"          "C"              "B"             "A"             "E"             "G"             "F"
        {COM_ROW(0, 0), COM_ROW(1, 0), COM_ROW(2, 0), COM_ROW(3, 0), COM_ROW(4, 0), COM_ROW(5, 0), COM_ROW(6, 0)}, // 9A- 11G
        {COM_ROW(0, 1), COM_ROW(1, 1), COM_ROW(2, 1), COM_ROW(3, 1), COM_ROW(4, 1), COM_ROW(5, 1), COM_ROW(6, 1)}, // 8A- 12G
        {COM_ROW(0, 2), COM_ROW(1, 2), COM_ROW(2, 2), COM_ROW(3, 2), COM_ROW(4, 2), COM_ROW(5, 2), COM_ROW(6, 2)}, // 7A- 13G

    };

    // clear dig data
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }

    int data = amount;
    if(data < 0)
    {
        return;
    }
    for (int i = 0; i < 3; i++)
    {
        int num = data % 10;
        int dig = dig_seg[num] & 0xff;
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);

            if (dig & 0x01)
            {
                disp_buf[row_idx] |= (0x01 << com_idx);
            }
            else
            {
                disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
            }
            dig = dig >> 1;
        }
        data = data / 10;
        if (i < 2 && data == 0)
        {
            break;
        }
    }
}

OSI_UNUSED static void prvLcdReceiveAmountClr(void)
{
    int row_idx = 0;
    int com_idx = 0;

    //  3位数字显示数组
    short recv_data_map[3][7] = {
        // "D"          "C"              "B"             "A"             "E"             "G"             "F"
        {COM_ROW(0, 0), COM_ROW(1, 0), COM_ROW(2, 0), COM_ROW(3, 0), COM_ROW(4, 0), COM_ROW(5, 0), COM_ROW(6, 0)}, // 9A- 11G
        {COM_ROW(0, 1), COM_ROW(1, 1), COM_ROW(2, 1), COM_ROW(3, 1), COM_ROW(4, 1), COM_ROW(5, 1), COM_ROW(6, 1)}, // 8A- 12G
        {COM_ROW(0, 2), COM_ROW(1, 2), COM_ROW(2, 2), COM_ROW(3, 2), COM_ROW(4, 2), COM_ROW(5, 2), COM_ROW(6, 2)}, // 7A- 13G

    };

    // clear dig data
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 7; j++)
        {
            row_idx = ROW(recv_data_map[i][j]);
            com_idx = COM(recv_data_map[i][j]);
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }
}
#endif

static void Paytm_LCD_Set_RS_En(bool en)
{
    short rs_flag_map = COM_ROW(0, 10);
    int row_idx = ROW(rs_flag_map);
    int com_idx = COM(rs_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Hook_En(bool en)
{
    short hook_flag_map = COM_ROW(2, 13);
    int row_idx = ROW(hook_flag_map);
    int com_idx = COM(hook_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Count_En(bool en)
{
#ifdef PAYTM_NEW_LCD
    short count_flag_map = COM_ROW(0, 11);
#else
    short count_flag_map = COM_ROW(7, 0);
#endif
    int row_idx = ROW(count_flag_map);
    int com_idx = COM(count_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_PayTm_En(bool en)
{
#ifdef PAYTM_NEW_LCD
    short paytm_flag_map = COM_ROW(3, 13);
#else
    short paytm_flag_map = COM_ROW(7, 1);
#endif
    int row_idx = ROW(paytm_flag_map);
    int com_idx = COM(paytm_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

#ifndef PAYTM_NEW_LCD
static void Paytm_LCD_Set_BT_En(bool en)
{
    short bt_flag_map = COM_ROW(5, 4);
    int row_idx = ROW(bt_flag_map);
    int com_idx = COM(bt_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Fm_En(bool en)
{
    short fm_flag_map = COM_ROW(4, 4);
    int row_idx = ROW(fm_flag_map);
    int com_idx = COM(fm_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

static void Paytm_LCD_Set_Wifi_Levl(int levl)
{
    int row_idx = 0;
    int com_idx = 0;
    short wifi_flag_map[] = {COM_ROW(3, 5), COM_ROW(2, 5), COM_ROW(1, 5), COM_ROW(0, 5)};
    if (levl > 4)
    {
        levl = 4;
    }
    osiMutexLock(lcdFlagMux);
    // clear
    for (int i = 0; i < sizeof(wifi_flag_map) / sizeof(wifi_flag_map[0]); i++)
    {
        row_idx = ROW(wifi_flag_map[i]);
        com_idx = COM(wifi_flag_map[i]);
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }

    // if (levl == 0)
    // {
    //     return;
    // }

    // set
    for (int i = 0; i < sizeof(wifi_flag_map) / sizeof(wifi_flag_map[0]); i++)
    {
        row_idx = ROW(wifi_flag_map[i]);
        com_idx = COM(wifi_flag_map[i]);
        if (i > levl)
        {
            osiMutexUnlock(lcdFlagMux);
            return;
        }
        else
        {
            disp_buf[row_idx] |= (0x01 << com_idx);
        }
    }
    osiMutexUnlock(lcdFlagMux);
}
#endif

void Paytm_LCD_Set_Volume_Levl(int levl)
{
    int row_idx = 0;
    int com_idx = 0;
#ifdef PAYTM_NEW_LCD
    short signal_flag_map[] = {COM_ROW(4, 13), COM_ROW(5, 13), COM_ROW(6, 13), COM_ROW(7, 13), COM_ROW(3, 14)};
#else
    short signal_flag_map[] = {COM_ROW(4, 5), COM_ROW(5, 5), COM_ROW(6, 5), COM_ROW(7, 5)};
#endif
    if (levl > 4)
    {
        levl = 4;
    }
    osiMutexLock(lcdFlagMux);
    // clear
    for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]); i++)
    {
        row_idx = ROW(signal_flag_map[i]);
        com_idx = COM(signal_flag_map[i]);
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }

    if (levl <= 0)
    {
        osiMutexUnlock(lcdFlagMux);
        return;
    }

    // set
    for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]); i++)
    {
        row_idx = ROW(signal_flag_map[i]);
        com_idx = COM(signal_flag_map[i]);
        if (i > levl)
        {
            osiMutexUnlock(lcdFlagMux);
            return;
        }
        else
        {
            disp_buf[row_idx] |= (0x01 << com_idx);
        }
    }
    osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_Mute(bool en)
{
    short paytm_flag_map[] = {COM_ROW(0, 13), COM_ROW(1, 13)};
    int row_idx;
    int com_idx;
    osiMutexLock(lcdFlagMux);
    for (int i = 0; i < sizeof(paytm_flag_map) / sizeof(paytm_flag_map[0]); i++){
        row_idx = ROW(paytm_flag_map[i]);
        com_idx = COM(paytm_flag_map[i]);
        if (en){
            disp_buf[row_idx] |= (0x01 << com_idx);
        }else{
            disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
        }
    }
    osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_Signal_Levl(int levl)
{
    int row_idx = 0;
    int com_idx = 0;
#ifdef PAYTM_NEW_LCD
    short signal_flag_map[] = {COM_ROW(3, 12), COM_ROW(2, 12), COM_ROW(1, 12), COM_ROW(0, 12), COM_ROW(7, 12)};
#else
    short signal_flag_map[] = {COM_ROW(0, 4), COM_ROW(1, 4), COM_ROW(2, 4), COM_ROW(3, 4)};
#endif
    if (levl > 4)
    {
        levl = 4;
    }
    osiMutexLock(lcdFlagMux);
    // clear
    for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]); i++)
    {
        row_idx = ROW(signal_flag_map[i]);
        com_idx = COM(signal_flag_map[i]);
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }

    // if (levl == 0)
    // {
    //     osiMutexUnlock(lcdFlagMux);
    //     return;
    // }

    // clear
    for (int i = 0; i < sizeof(signal_flag_map) / sizeof(signal_flag_map[0]); i++)
    {
        row_idx = ROW(signal_flag_map[i]);
        com_idx = COM(signal_flag_map[i]);
#ifdef PAYTM_NEW_LCD
        if (i > levl)
#else
        if (i >= levl)
#endif
        {
            osiMutexUnlock(lcdFlagMux);
            return;
        }
        else
        {
            disp_buf[row_idx] |= (0x01 << com_idx);
        }
    }
    osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_Power_Levl(int levl)
{
    int row_idx = 0;
    int com_idx = 0;
#ifdef PAYTM_NEW_LCD
    short power_flag_map[] = {COM_ROW(4, 14), COM_ROW(2, 14), COM_ROW(0, 14), COM_ROW(1, 14), COM_ROW(5, 14)};
    if (levl > 4)
    {
        levl = 4;
    }
#else
    short power_flag_map[] = {COM_ROW(2, 3), COM_ROW(4, 3), COM_ROW(0, 3), COM_ROW(1, 3), COM_ROW(5, 3), COM_ROW(6, 3)};
    if (levl > 5)
    {
        levl = 5;
    }
#endif
    osiMutexLock(lcdFlagMux);
    // clear
    for (int i = 0; i < sizeof(power_flag_map) / sizeof(power_flag_map[0]); i++)
    {
        row_idx = ROW(power_flag_map[i]);
        com_idx = COM(power_flag_map[i]);
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
// #ifdef PAYTM_NEW_LCD
//     if(levl == 0){
//         osiMutexUnlock(lcdFlagMux);
//         return;
//     }
// #endif
    // set
    for (int i = 0; i < sizeof(power_flag_map) / sizeof(power_flag_map[0]); i++)
    {
        row_idx = ROW(power_flag_map[i]);
        com_idx = COM(power_flag_map[i]);
        if (i > levl)
        {
            osiMutexUnlock(lcdFlagMux);
            return;
        }
        else
        {
            disp_buf[row_idx] |= (0x01 << com_idx);
        }
    }
    osiMutexUnlock(lcdFlagMux);
}
#ifdef PAYTM_NEW_LCD
void Paytm_LCD_Set_NoSignal_En(bool en)
{
    short fm_flag_map = COM_ROW(6, 12);
    int row_idx = ROW(fm_flag_map);
    int com_idx = COM(fm_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_NoSim_En(bool en)
{
    short fm_flag_map = COM_ROW(5, 12);
    int row_idx = ROW(fm_flag_map);
    int com_idx = COM(fm_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}

void Paytm_LCD_Set_NoConnect_En(bool en)
{
    short fm_flag_map = COM_ROW(4, 12);
    int row_idx = ROW(fm_flag_map);
    int com_idx = COM(fm_flag_map);
    osiMutexLock(lcdFlagMux);
    if (en)
    {
        disp_buf[row_idx] |= (0x01 << com_idx);
    }
    else
    {
        disp_buf[row_idx] &= ((0x01 << com_idx) ^ 0xff);
    }
    osiMutexUnlock(lcdFlagMux);
}
#endif
static void prvLcdStart(void)
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
static void prvLcdStop(void)
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
}

static int prvLcdCmdAck(void)
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
    GpioInitConfiguration(LCD_SDA, cfg);
    while (GpioGetLevel(LCD_SDA))
    {
        count++;
        nop();
        if (count > 100)
        {
            RTI_LOG("[LCD] ACK TIMEOUT! SDA is HIGH (count=%d)\r\n", count);
            break;
        }
    }
    SCL(1);
    nop();
    SCL(0);
    nop();
    GpioSetDirection(LCD_SDA, 1);
    SDA(0);
    nop();
    return (count <= 100) ? 0 : -1;
}

static void prvLcdSCLByte(char SDA)
{
    for (int i = 0; i < 8; i++)
    {
        SCL(0);
        nop();
        if (SDA & 0x01)
            SDA(1);
        else
            SDA(0);
        nop();
        SCL(1);
        nop();
        SDA >>= 1;
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
    GpioInitConfiguration(LCD_SDA, cfg);
}

/***************写命令函数**************/
static void SCL_cmd(unsigned char cmd)
{
    prvLcdStart();
    prvLcdSCLByte(cmd);
    int ret = prvLcdCmdAck();
    prvLcdStop();

    if (ret != 0) {
        RTI_LOG("[LCD] SCL_cmd(0x%02X) FAILED: No ACK!\r\n", cmd);
    } else {
        RTI_LOG("[LCD] SCL_cmd(0x%02X) OK (ACK received)\r\n", cmd);
    }

    nop();
}

static void dispDma(unsigned char address, char *data, unsigned int data_len)
{
	unsigned char i;
    unsigned char tmp;
    int ack_err = 0;

    RTI_LOG("[LCD] dispDma send: addr=0x%02X, len=%d\r\n", address, data_len);
    prvLcdStart();
    prvLcdSCLByte(address); // 起始地址
    if (prvLcdCmdAck() != 0) ack_err++;
    for (i = 0; i < data_len; i++)
    {
        tmp = data[i];
        prvLcdSCLByte(tmp);
        if (prvLcdCmdAck() != 0) ack_err++;
    }

    prvLcdStop();

    SCL_cmd(0x27); // 发送显示控制命令：正常模式，LINE模式，1/3偏压，5V
    if (ack_err > 0) {
        RTI_LOG("[LCD] dispDma done with %d ACK errors!\r\n", ack_err);
    } else {
        RTI_LOG("[LCD] dispDma success (all ACKs OK)\r\n");
    }
}

/***************向显存写数据**************/
/***************TM1726初始化函数**************/
static void init_tm1726()
{
    RTI_LOG("[LCD] init_tm1726 called\r\n");
    SCL_cmd(0x27);
}
extern void drvIsinkEnable(bool enabled, char level);
extern void drvIsinkPwmEnable(bool enabled, char level, char duty);
extern void osiSetPMEnableSleep(bool enable);

void app_test_backlight(void)
{
RTI_LOG("[LCD] Enabling Stable Backlight...\r\n");
    // 1. Backlight on rehte hue PMIC sleep band karein taaki flicker na ho
    osiSetPMEnableSleep(false);
    // 2. Stable Current Level (Level 3, Duty 0x30 / ~40%)
    drvIsinkPwmEnable(true, 3, 0x30);
}

void appLcdset(lcdParam_t lcd_buf)
{
#ifdef PAYTM_NEW_LCD
    bool newLed = true;
#else
    bool newLed = false;
#endif
    if(!newLed){
        // Paytm_LCD_Set_BT_En(lcd_buf.lcd_bt);
        // Paytm_LCD_Set_Fm_En(lcd_buf.lcd_fm);
        // Paytm_LCD_Set_Wifi_Levl(lcd_buf.lcd_wifi);
    }else{
        // Paytm_LCD_Set_Mute(lcd_buf.lcd_mute);
        // Paytm_LCD_Set_NoSignal_En(lcd_buf.lcd_NoSignal);
        // Paytm_LCD_Set_NoSim_En(lcd_buf.lcd_NoSim);
        // Paytm_LCD_Set_NoConnect_En(lcd_buf.lcd_NoConnect);
        prvLcdDecimalNumSet(lcd_buf.lcd_decimalnum);
        prvLcdFrontFillNumSet(lcd_buf.lcd_fill);
    }
    // Paytm_LCD_Set_PayTm_En(lcd_buf.lcd_paytm);
    // Paytm_LCD_Set_Power_Levl(lcd_buf.lcd_power);
    // Paytm_LCD_Set_Signal_Levl(lcd_buf.lcd_signal);
    // Paytm_LCD_Set_Volume_Levl(lcd_buf.lcd_vol);
    Paytm_LCD_Set_Hook_En(lcd_buf.lcd_hook);
    Paytm_LCD_Set_RS_En(lcd_buf.lcd_rs);
    Paytm_LCD_Set_Count_En(lcd_buf.lcd_count);
    // add by vipin: only display count digits if count is enabled, else clear to prevent rogue '0'
    if (lcd_buf.lcd_count) {
        prvLcdReceiveAmountSet(lcd_buf.lcd_amount);
    } else {
        prvLcdReceiveAmountClr();
    }
    prvLcdBigNumSet(lcd_buf.lcd_num);
#ifdef PAYTM_NEW_LCD
    appAllrefresh();
#else
    dispDma(0xc0, disp_buf, 18);  //0xc0 为LCD SEG0的起始位置,写入数据
#endif
}

void appLcdDisplayEnableALL(bool enbale)
{
    osiMutexLock(lcdFlagMux);
    if(enbale){
        memset(disp_buf, 0xff, sizeof(disp_buf));
    }else{
        memset(disp_buf, 0, sizeof(disp_buf));
    }
    
#ifdef PAYTM_NEW_LCD
    dispDma(0xc0, disp_buf, 15);  //0xc0 为LCD SEG0的起始位置,写入数据
#else
    dispDma(0xc0, disp_buf, 18);  //0xc0 为LCD SEG0的起始位置,写入数据
#endif
    osiMutexUnlock(lcdFlagMux);
    //osiThreadSleep(500 / 5);
}

void appLcdenable(void)
{
    // long num1 = 0;
    // int num2 = 0;

    RTI_LOG("[LCD] appLcdenable called\r\n");
    app_test_backlight(); 
    init_tm1726(); // 初始化
    nop();
    nop();

    // prvLcdReceiveAmountSet(num2);
    // Paytm_LCD_Set_BT_En(1);
    // Paytm_LCD_Set_Fm_En(1);
    // Paytm_LCD_Set_RS_En(1);
    // Paytm_LCD_Set_Count_En(1);
    Paytm_LCD_Set_PayTm_En(1);
    // Paytm_LCD_Set_Power_Levl(5);
    // Paytm_LCD_Set_Signal_Levl(4);
    // Paytm_LCD_Set_Wifi_Levl(4);
    // Paytm_LCD_Set_Volume_Levl(3);
    // prvLcdBigNumSet(num1);

#ifdef PAYTM_NEW_LCD
    dispDma(0xc0, disp_buf, 15);  //0xc0 为LCD SEG0的起始位置,写入数据
#else
    dispDma(0xc0, disp_buf, 18);  //0xc0 为LCD SEG0的起始位置,写入数据
#endif
    //osiThreadSleep(1000 / 5);
}

void appLcdenablecharger(void)
{
    // init_tm1726(); // 初始化
    nop();
    nop();

    Paytm_LCD_Set_Power_Levl(0);
#ifdef PAYTM_NEW_LCD
    dispDma(0xc0, disp_buf, 15);  //0xc0 为LCD SEG0的起始位置,写入数据
#else
    dispDma(0xc0, disp_buf, 18);  //0xc0 为LCD SEG0的起始位置,写入数据
#endif
}

int appLcdInit(void)
{

    if(lcdFlagMux == NULL){
        lcdFlagMux = osiMutexCreate();
    }else{
        return true;
    }

    prvLcdGpioInit();
    // CS(0);
    // init_tm1726();
    // CS(1);
    RTI_LOG("[LCD] appLcdInit done: CS(pin %d)=%d, SCL(pin %d)=%d, SDA(pin %d)=%d\r\n",
            LCD_CS, GpioGetLevel(LCD_CS), LCD_SCL, GpioGetLevel(LCD_SCL), LCD_SDA, GpioGetLevel(LCD_SDA));

    return 0;
}

void appnumclear(bool Amount,bool num)
{
    bool flag = false;

    if(true == Amount)
    {
        prvLcdReceiveAmountClr();
#ifdef PAYTM_NEW_LCD
        Paytm_LCD_Set_Count_En(false);
#endif
        flag = true;
    }
    if(true == num)
    {
        prvLcdBigNumClr();
#ifdef PAYTM_NEW_LCD
        prvLcdPointSet(0);
        Paytm_LCD_Set_RS_En(false);
        Paytm_LCD_Set_Hook_En(false);
#endif
        flag = true;
    }
    if(true == flag)
    {
        osiMutexLock(lcdFlagMux);
#ifdef PAYTM_NEW_LCD
        dispDma(0xc0, disp_buf, 15);  //0xc0 为LCD SEG0的起始位置,写入数据
#else
        dispDma(0xc0, disp_buf, 18);  //0xc0 为LCD SEG0的起始位置,写入数据
#endif
        osiMutexUnlock(lcdFlagMux);
    }
}

void appAllrefresh(void)
{
    osiMutexLock(lcdFlagMux);
#ifdef PAYTM_NEW_LCD
    dispDma(0xc0, disp_buf, 15);  //0xc0 为LCD SEG0的起始位置,写入数据
#else
    dispDma(0xc0, disp_buf, 18);  //0xc0 为LCD SEG0的起始位置,写入数据
#endif
    osiMutexUnlock(lcdFlagMux);
}

void appdatarefresh(int addr, int len)
{
    osiMutexLock(lcdFlagMux);
#ifdef PAYTM_NEW_LCD
    dispDma((0xc0 + addr), &disp_buf[addr], len);  //0xc0 为LCD SEG0的起始位置,写入数据
#else
    dispDma((0xc0 + addr), &disp_buf[addr], len);  //0xc0 为LCD SEG0的起始位置,写入数据
#endif
    osiMutexUnlock(lcdFlagMux);
}


#endif
