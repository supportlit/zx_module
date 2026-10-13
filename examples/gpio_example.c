/******************************************************************************
 ** File Name:      gpio_example.c                                            *
 ** Author:         chenwenxiu                                                *
 ** DATE:           2021-11-23                                                *
 ** Copyright:      Zhong Yun Xin an(Shenzhen) science and Technology Co.,Ltd.*
 **                 All Rights Reserved.                                      *
 ** Description:    This file contains gpio application example               *
 ******************************************************************************
 *******************************************************************************
 **                        Edit History                                        *
 ** ---------------------------------------------------------------------------*
 ** DATE           NAME                   Description:
 **----------------------------------------------------------------------------*
 ** 2021-11-23     chenwenxiu               Create.
 ******************************************************************************/
/*******************************************************************************
 ** Header Files
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "typedef.h"
#include "osi_api.h"
#include "kernel_version.h"
#include "mfpr_api.h"
#include "gpio_api.h"
#if defined(MODULE_ZX800SG_CA) || defined(MODULE_ZX800SG_SA)
#error
#include "drv_iomux.h"
#include "drv_def.h"
#endif

/*******************************************************************************
** MACROS
******************************************************************************/
#if defined(MODULE_ZX800SG_CA) || defined(MODULE_ZX800SG_SA)
#define GPIO_OUTPUT  	GPIO_55	//MAIN_RI 20
#define GPIO_INPUT		GPIO_05	//MAIN_DCD 21
#define GPIO_INTERRUPT	GPIO_37	//MAIN_DTR 19

#define PIN_OUTPUT		(55)
#define PIN_INPUT		(05)
#define PIN_INTERRUPT	(37)
#else
#define GPIO_OUTPUT  	GPIO_06	//NET_MODE 52
#define GPIO_INPUT		GPIO_02	//MAIN_DCD 48
#define GPIO_INTERRUPT	GPIO_03	//WAKEUP_IN 49

#define PIN_OUTPUT		(06)
#define PIN_INPUT		(02)
#define PIN_INTERRUPT	(03)
#endif/*MODULE_ZX800SG_CA*/

/*******************************************************************************
** Function Declerations
******************************************************************************/
static void GPIO_Main(void *param);
static void gpio_isr(void);

/*******************************************************************************
** Functions
******************************************************************************/
int appimg_enter(void *param)
{
    RTI_LOG("application image enter");
    RTI_LOG("kernelVersion: %s", kernelVersion());
    RTI_LOG("welcome, %s,%s", __DATE__, __TIME__);

	//create thread
	osiThreadCreate("GPIO_function", GPIO_Main, NULL, 231, 4*1024);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}

static void gpio_isr(void)
{
	RTI_LOG("GPIO Interrupt Comming !");
	GpioClearEdgeDetection(PIN_INTERRUPT);
}

/******************************************************************************
 * F@: GPIO_TestInit
 * set cmd: init gpio as Normal GPIO
 * test set NET_MODE pin of output¡¢set SLEEP_IND pin of input¡¢set STATUS pin of interrupt
 */
static void GPIO_TestInit(void)
{
	GPIOConfiguration interrutp_cfg;

	/*Multip Function Pin confige*/
    unsigned int mfp_gpio[] = {
        MFP_REG(GPIO_OUTPUT) | MFP_AF0 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(GPIO_INPUT) | MFP_AF0 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_REG(GPIO_INTERRUPT) | MFP_AF0 | MFP_DRIVE_VERY_SLOW | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
        MFP_EOC
    };
    mfp_config(mfp_gpio);

	/*GPIO function set*/
	GpioSetDirection(PIN_OUTPUT, GPIO_OUT_PIN); //OUTPUT
	GpioSetDirection(PIN_INPUT, GPIO_IN_PIN); //INPUT
	/*INTERRUPT*/
	memset(&interrutp_cfg, 0, sizeof(GPIOConfiguration));
	interrutp_cfg.pinDir = GPIO_IN_PIN;
	interrutp_cfg.pinPull = GPIO_PULL_DISABLE;
	interrutp_cfg.pinEd = GPIO_RISE_EDGE;
	interrutp_cfg.isr = gpio_isr;
	GpioInitConfiguration(PIN_INTERRUPT, interrutp_cfg);
}

/*GPIO test entry*/
static void GPIO_Main(void *param)
{
	RTI_LOG("GPIO Init");
	GPIO_TestInit();
	GPIOPinDirection pin1_dir, pin2_dir, pin3_dir;
	GPIOReturnCode pin1_level, pin2_level, pin3_level;

	while(1)
	{

		pin1_dir = GpioGetDirection(PIN_OUTPUT);
		pin2_dir = GpioGetDirection(PIN_INPUT);
		pin3_dir = GpioGetDirection(PIN_INTERRUPT);

		/*set output*/
		GpioSetLevel(PIN_OUTPUT, 0);
		pin1_level = GpioGetLevel(PIN_OUTPUT);
		pin2_level = GpioGetLevel(PIN_INPUT);
		pin3_level = GpioGetLevel(PIN_INTERRUPT);
		RTI_LOG("[Dir:level]--PIN1[%d,%d],PIN2[%d,%d],PIN3[%d,%d]",
			pin1_dir, pin1_level, pin2_dir, pin2_level, pin3_dir, pin3_level);
		osiThreadSleep(600);

		/*delay 3s*/
		GpioSetLevel(PIN_OUTPUT, 1);
		pin1_level = GpioGetLevel(PIN_OUTPUT);
		pin2_level = GpioGetLevel(PIN_INPUT);
		pin3_level = GpioGetLevel(PIN_INTERRUPT);
		RTI_LOG("[Dir:level]--PIN1[%d,%d],PIN2[%d,%d],PIN3[%d,%d]",
			pin1_dir, pin1_level, pin2_dir, pin2_level, pin3_dir, pin3_level);
		osiThreadSleep(600);
	}
}
