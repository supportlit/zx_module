#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "osi_api.h"
#include "drv_serial.h"

#define USERIAL_RX_BUF_SIZE (256 * 1024)
#define USERIAL_TX_BUF_SIZE (128 * 1024)

static drvSerial_t *serial = NULL;
static osiThread_t *serThread = NULL;

static void _UsrlCb(drvSerialEvent_t evt, unsigned long param)
{
    uint32_t vlen = 0;
    uint32_t rlen = 0;

    if (evt & DRV_SERIAL_EVENT_RX_ARRIVED){
        vlen = drvSerialReadAvail(serial);
        if(vlen){
            char *rbuf = (char *)malloc(vlen);
            assert(rbuf != NULL);

            rlen = drvSerialRead(serial, rbuf, vlen);
            if(rlen > 0){
                drvSerialWrite(serial, rbuf, rlen);
            }
            free(rbuf);
        }
    }
}


static void prvUsbSerialTask(void *param){
    char sendBuf[128] = {0};
    uint32_t sendLen = 0;
    uint32_t cnt = 0;

    uint32_t event = DRV_SERIAL_EVENT_RX_ARRIVED | DRV_SERIAL_EVENT_RX_OVERFLOW | DRV_SERIAL_EVENT_TX_COMPLETE;
 
    drvSerialCfg_t cfg = {0};
    cfg.tx_buf_size = USERIAL_TX_BUF_SIZE;
    cfg.rx_buf_size = USERIAL_RX_BUF_SIZE;
    cfg.event_mask = event;
    cfg.param = 0;
    cfg.event_cb = _UsrlCb;

    serial = drvSerialOpen(USERIAL_PORT_AT, &cfg);
    assert(serial);

    while (1)
    {
        sendLen = snprintf(sendBuf, sizeof(sendBuf), "Device send cnt:%ld\r\n", cnt++);
        drvSerialWrite(serial, sendBuf, sendLen);
        osiThreadSleep(1000/5);
    }
    drvSerialClose(serial);
    serial = NULL;
    osiThreadExit();
}

int appimg_enter(void *param)
{
    RTI_LOG("application image enter, param 0x%x", param);
    serThread = osiThreadCreate("USERIAL", prvUsbSerialTask, NULL, 35, 4*1024);
    assert(serThread != NULL);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}

