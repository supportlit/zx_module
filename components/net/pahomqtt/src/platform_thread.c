/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-23 19:26:27
 * @LastEditTime: 2020-04-25 08:44:24
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_thread.h"
#include "platform_memory.h"
#include "osi_api.h"

platform_thread_t *platform_thread_init(char * name,
                                        void (*entry)(void *),
                                        void * param,
                                        unsigned int stack_size,
                                        unsigned int priority,
                                        unsigned int tick)
{
    platform_thread_t *thread;
    // OSTaskRef TaskRef = NULL;

    // thread = platform_memory_alloc(sizeof(platform_thread_t));
    // if(NULL == thread)
    // {
    //     return NULL;
    // }

	/* coverity[leaked_storage] */
	thread = (platform_thread_t *)osiThreadCreate(name, entry, param, priority, stack_size);

    return thread;
}

void platform_thread_startup(platform_thread_t* thread)
{
    /*add print here*/
}


void platform_thread_stop(platform_thread_t* thread)
{
    osiThreadExit();

}

void platform_thread_start(platform_thread_t* thread)
{

}

void platform_thread_destroy(platform_thread_t* thread)
{
	osiThreadExit();
}


