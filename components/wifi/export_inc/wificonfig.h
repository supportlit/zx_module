#ifndef __WIFI_AUTOCFG_H
#define __WIFI_AUTOCFG_H

/* Customer settings now flow via compile definitions; header include kept for legacy builds. */
#ifdef CUSTOMER_CONFIG_HEADER
#include CUSTOMER_CONFIG_HEADER
#elif defined(__has_include)
#  if __has_include("customer_config.h")
#    include "customer_config.h"
#  endif
#else
#include "customer_config.h"
#endif

#endif