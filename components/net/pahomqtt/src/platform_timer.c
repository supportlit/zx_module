/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-14 22:02:07
 * @LastEditTime: 2020-02-19 23:53:50
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */

#include "platform_timer.h"
#include "osi_api.h"

#define U32_DIFF(a, b) (((a) >= (b)) ? ((a) - (b)) : (((a) + ((b) ^ 0xFFFFFFFF) + 1)))

void platform_timer_init(platform_timer_t* timer)
{
    timer->tick = osiGetTicks();
    timer->time_out = 0;
}

void platform_timer_cutdown(platform_timer_t* timer, unsigned int timeout)
{
	timer->tick = osiGetTicks();
    timer->time_out = timeout;
}

char platform_timer_is_expired(platform_timer_t* timer)
{
    uint32_t tick_now = 0;
    uint32_t tick_diff = 0;

    tick_now = osiGetTicks();
    tick_diff = U32_DIFF(tick_now, timer->tick);

    if (tick_diff * MQTT_RTOS_MS_ONE_TICK >= timer->time_out) {
        return 1;
    } else {
        return 0;
    }
}

int platform_timer_remain(platform_timer_t* timer)
{
    uint32_t tick_now = 0;
    uint32_t tick_diff = 0;

    tick_now = osiGetTicks();
    tick_diff = U32_DIFF(tick_now, timer->tick);

    if (tick_diff * MQTT_RTOS_MS_ONE_TICK >= timer->time_out) {
        return 0;
    } else {
        return (timer->time_out - tick_diff * MQTT_RTOS_MS_ONE_TICK);
    }
}

unsigned long platform_timer_now(void)
{
    return (unsigned long) osiGetTicks();
}

void platform_timer_usleep(unsigned long usec)
{
    uint32_t ms = 0;
    if (usec != 0) {
        ms = usec / 1000;
        if (ms == 0) {ms = 1;}
    }

    ms = (ms <= MQTT_RTOS_MS_ONE_TICK ? 1 : (ms / MQTT_RTOS_MS_ONE_TICK));
    osiThreadSleep(ms); /*tick*/
}

void platform_timer_msleep(uint32_t ms)
{
    ms = (ms <= MQTT_RTOS_MS_ONE_TICK ? 1 : (ms / MQTT_RTOS_MS_ONE_TICK));
    osiThreadSleep(ms); /*tick*/
}

