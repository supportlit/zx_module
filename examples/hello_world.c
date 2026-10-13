/* All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include "osi_api.h"
#include "kernel_version.h"

int appimg_enter(void *param)
{
    RTI_LOG("application image enter");
    RTI_LOG("kernelVersion: %s", kernelVersion());
    RTI_LOG("welcome, %s,%s", __DATE__, __TIME__);
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}