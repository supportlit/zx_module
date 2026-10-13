/******************************************************************************
 ** File Name:      flash_example.c                                           *
 ** Author:         chenwenxiu                                                *
 ** DATE:           2021-11-24                                                *
 ** Copyright:      Zhong Yun Xin an(Shenzhen) science and Technology Co.,Ltd.*
 **                 All Rights Reserved.                                      *
 ** Description:    This file contains flash application example              *
 ******************************************************************************
 *******************************************************************************
 **                        Edit History                                        *
 ** ---------------------------------------------------------------------------*
 ** DATE           NAME                   Description:
 **----------------------------------------------------------------------------*
 ** 2021-11-24     chenwenxiu               Create.
 ******************************************************************************/
/*******************************************************************************
 ** Header Files
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "typedef.h"
#include "osi_api.h"
#include "kernel_version.h"
#include "flash_api.h"

/*******************************************************************************
** MACROS
******************************************************************************/
/*this flash address only use flash API test,
If you must use FLASH API directly without using a FS API, please contact FAE */
#define FLASH_TEST_ADDRESS		0x00D91000
#define FLASH_TEST_SIZE			0x00002000

/*******************************************************************************
** Type Definitions
******************************************************************************/
typedef struct
{
	UINT16 num;
	char name[16];
	char card[24];
} TestInfo_t;

/*******************************************************************************
** Variables
******************************************************************************/
static TestInfo_t flashInfo;

/*******************************************************************************
** Function Declerations
******************************************************************************/
static void FLASH_Main(void *param);

/*******************************************************************************
** Functions
******************************************************************************/
int appimg_enter(void *param)
{
    RTI_LOG("application image enter");
    RTI_LOG("kernelVersion: %s", kernelVersion());
    RTI_LOG("welcome, %s,%s", __DATE__, __TIME__);

	//create thread
	osiThreadCreate("FILE_function", FLASH_Main, NULL, 231, 4*1024);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}

static void FLASH_TestRead(void)
{
	memset(&flashInfo, 0, sizeof(TestInfo_t));
	flash_read(FLASH_TEST_ADDRESS, (unsigned int)&flashInfo, sizeof(TestInfo_t));

	RTI_LOG("num	 :%d", flashInfo.num);
	RTI_LOG("name	 :%s", flashInfo.name);
	RTI_LOG("card id :%s", flashInfo.card);
}

static void FLASH_TestWrite(void)
{
	memset(&flashInfo, 0, sizeof(TestInfo_t));
	flash_read(FLASH_TEST_ADDRESS, (unsigned int)&flashInfo, sizeof(TestInfo_t));

	flashInfo.num = 110;
	memcpy(flashInfo.name, "www.chinainfosafe.com", sizeof("www.chinainfosafe.com"));
	memcpy(flashInfo.card, "19926453380", sizeof("19926453380"));

	flash_erase(FLASH_TEST_ADDRESS, sizeof(TestInfo_t));
	flash_write(FLASH_TEST_ADDRESS, (unsigned int)&flashInfo, sizeof(TestInfo_t));
}

/*flash test entry*/
static void FLASH_Main(void *param)
{
	UINT64 flashId = 0;

	//flashId = flash_read_uid();
	RTI_LOG("the flash id is 0x%X", flashId);

	FLASH_TestRead();
	osiThreadSleep(1000);// 1000*5ms

	FLASH_TestWrite();
	RTI_LOG("nvm addr=0x%X, size=0x%X",flash_read_addr("nvm"), flash_read_size("nvm"));
	FLASH_TestRead();
	osiThreadSleep(1000);// 1000*5ms

	appimg_exit();
}
