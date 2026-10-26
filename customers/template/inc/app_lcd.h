#ifndef _APP_LCD_H_
#define _APP_LCD_H_

#include "osi_api.h"
#include "osi_compiler.h"
#include "framework_config.h"
#include "customer_config.h"

// LCD Feature Enable Switch (1 = Enabled, 0 = Disabled/Normal Run)
#ifndef CUSTOMER_ENABLE_LCD
#define CUSTOMER_ENABLE_LCD     1
#endif

typedef struct 
{
    bool lcd_hook;
    bool lcd_rs;
    bool lcd_count;
    uint32_t lcd_amount;
    uint64_t lcd_num;
    uint32_t lcd_decimalnum;
    uint32_t lcd_fill;
} lcdParam_t;

#if defined(CUSTOMER_ENABLE_LCD) && (CUSTOMER_ENABLE_LCD == 1)

int appLcdInit(void);
void appLcdenable(void);
void appLcdenablecharger(void);
void appLcdDisplayEnableALL(bool enbale);
void appLcdset(lcdParam_t lcd_buf);
void appnumclear(bool Amount, bool num);

void appAllrefresh(void);
void appdatarefresh(int addr, int len);

void prvLcdSecCodeSet(const long amount);
void Paytm_LCD_Set_NoConnect_En(bool en);
void Paytm_LCD_Set_NoSim_En(bool en);
void Paytm_LCD_Set_NoSignal_En(bool en);
void Paytm_LCD_Set_Mute(bool en);
void Paytm_LCD_Set_Power_Levl(int levl);
void Paytm_LCD_Set_PayTm_En(bool en);
void Paytm_LCD_Set_Signal_Levl(int levl);
void Paytm_LCD_Set_Volume_Levl(int levl);
void appLcdShowTextLIT(void);
void appLcdShowTextHitachi(void);
void appLcdShowTime(void);
void appLcdShowDate(void);
void appLcdShowTimeVal(int hour, int min, int sec);
void appLcdShowDateVal(int day, int mon, int year);

#else /* CUSTOMER_ENABLE_LCD == 0: LCD Disabled / Normal Run Stubs (add by vipin) */

static inline int appLcdInit(void) { return 0; }
static inline void appLcdenable(void) {}
static inline void appLcdenablecharger(void) {}
static inline void appLcdDisplayEnableALL(bool enbale) { (void)enbale; }
static inline void appLcdset(lcdParam_t lcd_buf) { (void)lcd_buf; }
static inline void appnumclear(bool Amount, bool num) { (void)Amount; (void)num; }

static inline void appAllrefresh(void) {}
static inline void appdatarefresh(int addr, int len) { (void)addr; (void)len; }

static inline void prvLcdSecCodeSet(const long amount) { (void)amount; }
static inline void Paytm_LCD_Set_NoConnect_En(bool en) { (void)en; }
static inline void Paytm_LCD_Set_NoSim_En(bool en) { (void)en; }
static inline void Paytm_LCD_Set_NoSignal_En(bool en) { (void)en; }
static inline void Paytm_LCD_Set_Mute(bool en) { (void)en; }
static inline void Paytm_LCD_Set_Power_Levl(int levl) { (void)levl; }
static inline void Paytm_LCD_Set_PayTm_En(bool en) { (void)en; }
static inline void Paytm_LCD_Set_Signal_Levl(int levl) { (void)levl; }
static inline void Paytm_LCD_Set_Volume_Levl(int levl) { (void)levl; }
static inline void appLcdShowTextLIT(void) {}
static inline void appLcdShowTextHitachi(void) {}
static inline void appLcdShowTime(void) {}
static inline void appLcdShowDate(void) {}
static inline void appLcdShowTimeVal(int hour, int min, int sec) { (void)hour; (void)min; (void)sec; }
static inline void appLcdShowDateVal(int day, int mon, int year) { (void)day; (void)mon; (void)year; }

#endif /* CUSTOMER_ENABLE_LCD */

#endif /* _APP_LCD_H_ */