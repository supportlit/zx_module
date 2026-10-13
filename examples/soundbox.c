/* All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include "osi_api.h"
#include "kernel_version.h"

#define write32(addr, val)                          (*(volatile unsigned int *)(addr) = (val))
#define read32(addr)                                (*(volatile unsigned int *)(addr))

extern void app_main(void);

static void prvMainTask(void *param){
    volatile unsigned int data;

    *(volatile uint32_t*)(0xd4090128) = 0x4842414;

    *(volatile unsigned long*)0xd401e1bc = 0xc0c1;
    *(volatile unsigned long*)0xd401e1bc = 0xc0c1;
    *(volatile unsigned long*)0xd401e1c0 = 0xc0c1;

    data = read32(0xD401E000 + 0x1A8);
    data=data&0xE7FF;           // set DS=0;
    write32((0xD401E000 + 0x1A8),data);

    data = read32(0xD401E000 + 0x1AC);
    data=data&0xE7FF;           // set DS=0;
    write32((0xD401E000 + 0x1AC),data);
//
    *(volatile uint32_t*)0xd401e0c8 = 0xb040;
    *(volatile uint32_t*)0xd401e0f0 = 0xb040;
    *(volatile uint32_t*)0xd401e32c = 0xb040;

    *(volatile uint32_t*)0xd401e1a8 = 0x1c01;
    *(volatile uint32_t*)0xd401e1ac = 0x1c01;

    app_main();
    osiThreadExit();
}

OSI_TARGET_THUMB_ISA int appimg_enter(void *param)
{
    RTI_LOG("application image enter");
    RTI_LOG("kernelVersion: %s", kernelVersion());
    // RTI_LOG("welcome, %s,%s", __DATE__, __TIME__);

    // jumo to soundBox app 
    osiThreadCreate("app_main", prvMainTask,NULL, 100, 16*1024);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}