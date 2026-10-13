/* All rights reserved.
 *
 */

#ifndef _DRV_RTC_H_
#define _DRV_RTC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <osi_api.h>

typedef enum {
    RTC_TIME_SYNC_NITZ = 0,
    RTC_TIME_SYNC_NTP
}rtcTimeSyncType_e;

typedef void (*rtcTimeSyncCB)(rtcTimeSyncType_e type, uint32_t timezone, osiTime_t *time);

void drvRtcTimeSyncRegiterCB(rtcTimeSyncCB cb);

#ifdef __cplusplus
}
#endif

#endif // _DRV_RTC_H_
