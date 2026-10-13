/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-14 22:02:07
 * @LastEditTime: 2020-02-19 23:53:50
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_memory.h"
#include "osi_api.h"

void *platform_memory_alloc(size_t size)
{
    char *ptr = NULL;
    ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
        return (void *)ptr;
    } else {
        return NULL;
    }
}

void *platform_memory_calloc(size_t num, size_t size)
{
	char *ptr = NULL;
    ptr = malloc(size * num);
    if (ptr)
	{
        memset(ptr, 0, size);
        return (void *)ptr;
    }
	else
	{
        return NULL;
    }
}

void platform_memory_free(void *ptr)
{
    if (ptr) {free(ptr);}
}

