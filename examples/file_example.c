/******************************************************************************
 ** File Name:      file_example.c                                            *
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
#include "fs_api.h"

/*******************************************************************************
** MACROS
******************************************************************************/
#define TEST_EXIST_FILE "psm.dat"
#define TEST_CREATE_FILE "test.txt"

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
static TestInfo_t fileInfo;

/*******************************************************************************
** Function Declerations
******************************************************************************/
static void FILE_Main(void *param);

/*******************************************************************************
** Functions
******************************************************************************/
int appimg_enter(void *param)
{
    RTI_LOG("application image enter");
    RTI_LOG("kernelVersion: %s", kernelVersion());
    RTI_LOG("welcome, %s,%s", __DATE__, __TIME__);

	//create thread
	osiThreadCreate("FILE_function", FILE_Main, NULL, 231, 4*1024);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}

static void FILE_TestFile(void)
{
	int flen = 0;
	char *buf = NULL;
    int fd = 0;
	size_t size = 0;

    flen = fs_size(TEST_CREATE_FILE);
    if (flen <= 0 )
	{
        /* not exit create and init*/
        RTI_LOG("test.txt file %s not exit", TEST_CREATE_FILE);

		//ceate file
		fd = fs_open(TEST_CREATE_FILE, "wb+");
		if(fd <= 0)
		{
			RTI_LOG("Open test.txt file error fd:%d", fd);
			return;
		}

		memset(&fileInfo, 0, sizeof(TestInfo_t));
		fileInfo.num = 26;
		memcpy(fileInfo.name, "cooh2046", strlen("cooh2046"));
		memcpy(fileInfo.card, "362132198407275948", strlen("362132198407275948"));
		size = fs_write((void *)&fileInfo, sizeof(TestInfo_t), 1, fd);
		if(size <= 0)
		{
			RTI_LOG("write %s file error", TEST_CREATE_FILE);
			fs_close(fd);
		}

		RTI_LOG("write success!!!");
		fs_close(fd);
		return;
    }

	/*the file is exit, read data*/
    buf = osiMalloc(flen);
    if(buf == NULL)
	{
        RTI_LOG("malloc SN read buffer error");
        return;
    }

    memset(buf, 0, flen);
    RTI_LOG("test file %s size:%d", TEST_CREATE_FILE, flen);
    fd = fs_open(TEST_CREATE_FILE, "rb+");
    if(fd <= 0)
	{
        osiFree(buf);
        RTI_LOG("Open device sn file error fd:%d", fd);
        return;
    }

	size = fs_read(buf, 1, flen, fd);
	if(size != flen)
	{
		osiFree(buf);
		RTI_LOG("file info config file read error %d", size);
		fs_close(fd);
		return;
	}

	RTI_LOG("read data len %d", size);
	{
		TestInfo_t	readInfo;
		memcpy(&readInfo, buf, size);
		RTI_LOG("num     :%d", readInfo.num);
		RTI_LOG("name    :%s", readInfo.name);
		RTI_LOG("card id :%s", readInfo.card);
	}
	osiFree(buf);
	buf = NULL;

	fs_close(fd);
}

/*FILE test entry*/
static void FILE_Main(void *param)
{
	uint32_t fileSize = 0;
	uint32_t fileSysteSize = 0;

	fileSysteSize = fs_freeSize();
	RTI_LOG("the file system free size %d Bytes", fileSysteSize);

	fileSize = fs_size(TEST_EXIST_FILE);
	RTI_LOG("the %s is %d", TEST_EXIST_FILE, fileSize);

	while(1)
	{
		FILE_TestFile();
		osiThreadSleep(1000);// 1000*5ms
	}
}
