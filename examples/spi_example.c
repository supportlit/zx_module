/* All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "osi_api.h"
#include "mfpr_api.h"
#include "spi_api.h"

static void prvSPIThread(void *param)
{
	char *send  = malloc(100);
	char *recv  = malloc(100);
    RTI_LOG("TEST START");
	memset(send,0x00,100);
	memset(recv,0x00,100);
	memset(send,0x31,40);
	RTI_LOG("send_before=%s",send);

	CIS_SPI_Initialize(SPI_MODE0,SPI_CLK_26MHZ);

	CIS_SPI_DataTransfer(recv, send, 40);

	RTI_LOG("recv_after=%s",recv);
	RTI_LOG("send_after=%s",send);

	free(send);
	free(recv);
    
    RTI_LOG("TEST FINISH");
    osiThreadExit();   
}

int appimg_enter(void *param)
{
    osiThreadCreate("spi example", prvSPIThread, NULL, 100, 1024*3);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}