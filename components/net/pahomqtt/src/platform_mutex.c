/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-15 18:27:19
 * @LastEditTime : 2020-01-08 20:23:13
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_mutex.h"
#include "platform_timer.h"
#include "osi_api.h"



int platform_mutex_init(platform_mutex_t* m)
{
    if(m == NULL){
        return -1;
    }
    m->mutex = osiMutexCreate();
    if (NULL == m->mutex)
	{
        return 0;
    }
	else
	{
        return -1;
    }
}

int platform_mutex_lock(platform_mutex_t* m)
{
    if(m == NULL){
        return -1;
    }

    osiMutexLock(m->mutex);
    return 0;
}

int platform_mutex_unlock(platform_mutex_t* m)
{
    if(m == NULL){
        return -1;
    }

    osiMutexUnlock(m->mutex);
    return 0;
}

int platform_mutex_destroy(platform_mutex_t* m)
{
    if(m == NULL){
        return -1;
    }
    osiMutexDelete(m->mutex);
    return 0;
}
