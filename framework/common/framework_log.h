/**
 * @file framework_log.h
 * @brief Framework日志模块
 * @details 提供统一的日志接口,支持USB(printf)和UART(RTI_LOG)双输出
 */

#ifndef __FRAMEWORK_LOG_H__
#define __FRAMEWORK_LOG_H__

#include <stdio.h>
#include "osi_api.h"  // For RTI_LOG

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 日志输出配置
 * ========================================================================== */

/**
 * @brief 日志输出方式选择
 * @details 编译时定义:
 *          -DLOG_OUTPUT_USB   : 使用USB输出(printf)
 *          -DLOG_OUTPUT_UART  : 使用UART输出(RTI_LOG)
 *          -DLOG_OUTPUT_BOTH  : 同时输出到USB和UART(默认)
 */
#if !defined(LOG_OUTPUT_USB) && !defined(LOG_OUTPUT_UART) && !defined(LOG_OUTPUT_BOTH)
#define LOG_OUTPUT_UART  // 默认UART输出
#endif

/* 日志输出宏定义 */
#if defined(LOG_OUTPUT_BOTH)
    #define FRAMEWORK_LOG(fmt, ...) \
        do { \
            printf(fmt, ##__VA_ARGS__); \
            RTI_LOG(fmt, ##__VA_ARGS__); \
        } while(0)
#elif defined(LOG_OUTPUT_USB)
    #define FRAMEWORK_LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#elif defined(LOG_OUTPUT_UART)
    #define FRAMEWORK_LOG(fmt, ...) RTI_LOG(fmt, ##__VA_ARGS__)
#else
    #define FRAMEWORK_LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#endif

/* ============================================================================
 * 日志级别定义
 * ========================================================================== */
typedef enum {
    LOG_LEVEL_ERROR = 0,    /**< 错误 */
    LOG_LEVEL_WARN  = 1,    /**< 警告 */
    LOG_LEVEL_INFO  = 2,    /**< 信息 */
    LOG_LEVEL_DEBUG = 3     /**< 调试 */
} log_level_t;

/* ============================================================================
 * 日志宏定义 - 兼容OSI_LOG系列
 * ========================================================================== */

/**
 * @brief 错误日志
 * @param tag 标签ID(兼容原OSI_LOGE,第一个参数保留但不使用)
 * @param fmt 格式化字符串
 */
#define OSI_LOGE(tag, fmt, ...) \
    FRAMEWORK_LOG("[ERROR][%s][%s:%d] " fmt "\n", tag, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/**
 * @brief 警告日志
 * @param tag 标签ID(兼容原OSI_LOGW,第一个参数保留但不使用)
 * @param fmt 格式化字符串
 */
#define OSI_LOGW(tag, fmt, ...) \
    FRAMEWORK_LOG("[WARN ][%s][%s:%d] " fmt "\n", tag, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/**
 * @brief 信息日志
 * @param tag 标签ID(兼容原OSI_LOGI,第一个参数保留但不使用)
 * @param fmt 格式化字符串
 */
#define OSI_LOGI(tag, fmt, ...) \
    FRAMEWORK_LOG("[INFO ][%s][%s:%d] " fmt "\n", tag, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/**
 * @brief 调试日志
 * @param tag 标签ID(兼容原OSI_LOGD,第一个参数保留但不使用)
 * @param fmt 格式化字符串
 */
#define OSI_LOGD(tag, fmt, ...) \
    FRAMEWORK_LOG("[DEBUG][%s][%s:%d] " fmt "\n", tag, __FUNCTION__, __LINE__, ##__VA_ARGS__)

/* ============================================================================
 * 简化版日志宏
 * ========================================================================== */

/**
 * @brief 简化版日志宏,不带标签
 */
#define LOG_E(fmt, ...)  OSI_LOGE(0, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...)  OSI_LOGW(0, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...)  OSI_LOGI(0, fmt, ##__VA_ARGS__)
#define LOG_D(fmt, ...)  OSI_LOGD(0, fmt, ##__VA_ARGS__)

void framework_log_buf_dump(const char *tag, const void *buffer, size_t length);

/* ============================================================================
 * 条件编译控制
 * ========================================================================== */

/**
 * @brief 日志级别控制
 * @details 可在编译时定义LOG_LEVEL来控制日志输出级别
 *          例如: -DLOG_LEVEL=LOG_LEVEL_INFO
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#if LOG_LEVEL < LOG_LEVEL_DEBUG
#undef OSI_LOGD
#define OSI_LOGD(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL < LOG_LEVEL_INFO
#undef OSI_LOGI
#define OSI_LOGI(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL < LOG_LEVEL_WARN
#undef OSI_LOGW
#define OSI_LOGW(tag, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL < LOG_LEVEL_ERROR
#undef OSI_LOGHEX
#define OSI_LOGHEX(tag, buf, len) framework_log_buf_dump(tag, buf, len)
#endif

void framework_log_factory_mode(int enbale);

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_LOG_H__ */
