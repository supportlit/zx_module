/* All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "typedef.h"
#include "osi_api.h"
#include "drv_i2c.h"
#include "mfpr_api.h"
#include "mfpr.h"
#include "gpio_api.h"


#define NFC_SLAVE_ADDR	(0x28)  //0x14 0x28 0x50


void prvGpioInit(void) {
    unsigned int mfp_i2c[] = {

		MFP_REG(GPIO_10) | MFP_AF1 | MFP_DRIVE_FAST | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE, 
        MFP_REG(GPIO_11)   | MFP_AF1   | MFP_DRIVE_FAST | MFP_PULL_NONE | MFP_SLEEP_DIR | MFP_LPM_EDGE_NONE,

        MFP_EOC
    };
    mfp_config(mfp_i2c);
}

static void prvI2cExample(void *param)
{
	int ret = 0;
	osiThreadSleep(100);
	ret = I2C_write_byte(DRV_I2C_PORT_1, NFC_SLAVE_ADDR, 0x01, 0x0F);
	RTI_LOG("I2C_write_byte %d, %x", ret,  ret);
	osiThreadSleep(100);
	ret = I2C_read_byte(DRV_I2C_PORT_1, NFC_SLAVE_ADDR, 0x37);
	RTI_LOG("I2C_read_byte %d, %x", ret,  ret);
	osiThreadSleep(100);

    osiThreadExit();
}

void prvI2cTest(drvI2cPort_e port) {
	char name[12] = {0};
	prvGpioInit();
	I2C_init(port,DRV_I2C_STANDARD_MODE); // DRV_I2C_STANDARD_MODE   DRV_I2C_FAST_MODE
	snprintf(name, sizeof(name), "U%dTest", port);
	osiThreadCreate(name, prvI2cExample, (void *)&port, 100, 4*1024);
}

int appimg_enter(void *param)
{
	prvI2cTest(DRV_I2C_PORT_1);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}
