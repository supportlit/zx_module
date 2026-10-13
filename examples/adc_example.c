/******************************************************************************
 ** File Name:      flash_example.c                                           *
 ** Author:         chenwenxiu                                                *
 ** DATE:           2021-11-24                                                *
 ** Copyright:      Zhong Yun Xin an(Shenzhen) science and Technology Co.,Ltd.*
 **                 All Rights Reserved.                                      *
 ** Description:    This file contains ADC application example                *
 ******************************************************************************
 *******************************************************************************
 **                        Edit History                                        *
 ** ---------------------------------------------------------------------------*
 ** DATE           NAME                   Description:
 **----------------------------------------------------------------------------*
 ** 2021-11-24     chenwenxiu               Create.
 ** 2022-07_25	   chenwenxiu 				add ASR 1606 ADC example
 ******************************************************************************/
/*******************************************************************************
 ** Header Files
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "typedef.h"
#include "osi_api.h"
#include "kernel_version.h"
#include "drv_adc.h"

/*******************************************************************************
** ATTENTION
******************************************************************************/
/*
1、The ZX600CG Module has two ADC channels ADC0(Pin 19) ADC1(Pin 20)
   The ZX600RG Module has five ADC channels ADC0(Pin 19) ADC1(Pin 20) ADC2(Pin 113) ADC3(Pin 114) ADC4(Pin 140)
   The ZX800SG Module has three ADC channels ADC0(Pin 9) ADC1(Pin 96) ADC3(unelicited)
2、drvRtpAdcRead() can read ADC0 value. drvVbatRead() can rea ADC1 value.
3、The maximum check voltage is 1.8V, over which need use the divider circuit.
*/

/*******************************************************************************
** MACROS
******************************************************************************/
//#define MODULE_TYPE_ZX600RG //the compile macro for ZX600RG

#ifdef MODULE_TYPE_ZX600RG
/** 
* @brief adc config struct 
*/
typedef struct 
{
    uint8_t channel;   //adc channel 1~5
    uint8_t mode;      //adc mode
    uint8_t opt2;      //apb spare3 23:16(bit5:4 bias current and reference source, bit4 = 0: internel 1.2V reference)
    uint8_t opt3;      //apb spare3 31:24(bit2:0 adc sample delay clock; bit4:3 clock divider for AUXADC clock)
    uint8_t cnt;       //sample cnt that is used to calculate average value
}AdcCfg_t;
#endif/*MODULE_TYPE_ZX600RG*/

/*******************************************************************************
** Function Declerations
******************************************************************************/
static void ADC_Main(void *param);

/*******************************************************************************
** Functions
******************************************************************************/
int appimg_enter(void *param)
{
    RTI_LOG("application image enter");
    RTI_LOG("kernelVersion: %s", kernelVersion());
    RTI_LOG("welcome, %s,%s", __DATE__, __TIME__);

	//create thread
	osiThreadCreate("FILE_function", ADC_Main, NULL, 231, 4*1024);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}

#ifdef MODULE_TYPE_ZX600RG
static uint32_t getAdcValue(uint8_t adcIndex)
{
	AdcCfg_t adc = {adcIndex, 0x00, 0x57, 0x0A, 0x02};

	return drvAdcRead((void *)(&adc));
}
#endif/*MODULE_TYPE_ZX600RG*/

/*flash test entry*/
static void ADC_Main(void *param)
{
	uint32_t adc1 = 0;
	uint32_t adc2 = 0;

	while(1)
	{
		adc1 = drvRtpAdcRead();
		adc2 = drvVbatRead();	
	#ifdef MODULE_TYPE_ZX600RG
	  {
		uint32_t adc3 = getAdcValue(0x03);
		uint32_t adc4 = getAdcValue(0x04);
		uint32_t adc5 = getAdcValue(0x05);

		RTI_LOG("adc1=%d, adc2=%d, adc3=%d, adc4=%d, adc5=%d", adc1, adc2, adc3, adc4, adc5);
	  }
	#else
		RTI_LOG("adc1=%d, adc2=%d", adc1, adc2);
	#endif/*MODULE_TYPE_ZX600RG*/
		
		osiThreadSleep(600);// 600*5ms = 3s
	}

	appimg_exit();
}