/* All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "osi_api.h"
#include "drv_uart.h"
#include "mfpr_api.h"
#include "mfpr.h"

drvUart_t *uart_dev[DRV_UART_PORT_MAX] = {NULL};

void prvGpioInit(void){
    unsigned int mfp_uart[] = {
		// UART_2
		MFP_REG(GPIO_51) | MFP_AF1 | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE, 
        MFP_REG(GPIO_52)   | MFP_AF1   | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
		// UART_3
		MFP_REG(GPIO_14) | MFP_AF7 | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE, 
        MFP_REG(GPIO_15)   | MFP_AF7   | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,
		// UART_4
		MFP_REG(GPIO_00) | MFP_AF7 | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE, 
        MFP_REG(GPIO_01)   | MFP_AF7   | MFP_DRIVE_MEDIUM | MFP_PULL_HIGH | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE, 
         
        MFP_EOC
    };
    mfp_config(mfp_uart);
}

static void prvUartEventCB(drvUartEvent_e evt, void *ctx){
	int port = (int)ctx;
	
	if(evt == DRV_UART_EVENT_RX_ARRIVED){
		int rlen = drvUartReadAvail(uart_dev[port]);
		if(rlen <= 0){
			return;
		}
		char *recv  = malloc(rlen +1);
		drvUartReceive(uart_dev[port], recv, rlen);
		recv[rlen] = 0;
		RTI_LOG("[%d]RECV: %s",port, recv);
		free(recv);
	}else{
		RTI_LOG("other event: 0x%x",evt);
	}
}

static void prvUartExample(void *param)
{
    char buf[128] = {0};
	int cnt = 0;
	int slen = 0;
	drvUartPort_e port = (drvUartPort_e)param;
	drvUartCfg_t cfg = {
		.baud = DRV_UART_BAUD_115200,
		.data_bits = DRV_UART_DATA_BITS_8,
		.parity = DRV_UART_NO_PARITY,
		.stop_bits = DRV_UART_STOP_BITS_1,
		.rx_buf_size = 10*1024,
		.tx_buf_size = 10*1024,
		.event_cb = prvUartEventCB,
		.event_mask = DRV_UART_EVENT_RX_ARRIVED|DRV_UART_EVENT_RX_OVERFLOW,
		.event_cb_ctx = (void *)port,
	};
	uart_dev[port] = drvUartOpen(port, &cfg);

	while (1)
    {
        if(uart_dev[port] == NULL){
			RTI_LOG("Open error");
		}else{
			slen = snprintf(buf, sizeof(buf), "[%d]Send cnt: %d\n", port, cnt++);
			drvUartSend(uart_dev[port], buf, slen);
		}

        osiThreadSleep(1000/5);
    }
	drvUartClose(uart_dev[port]);
    osiThreadExit();
}

void prvUartTest(drvUartPort_e port){
	char name[12] = {0};
	prvGpioInit();
	snprintf(name, sizeof(name), "U%dTest", port);
	osiThreadCreate(name, prvUartExample, (void *)port, 100, 4*1024);
}


int appimg_enter(void *param)
{
	prvUartTest(DRV_UART_PORT_2);
	prvUartTest(DRV_UART_PORT_3);
	prvUartTest(DRV_UART_PORT_4);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}