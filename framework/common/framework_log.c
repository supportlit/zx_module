/* Copyright (C) 2018 ZxInfoTek Technologies Limited and/or its affiliatesZX.
 * All rights reserved.
 *
 * This software is supplied "AS IS" without any warranties.
 * ZX assumes no responsibility or liability for the use of the software,
 * conveys no license or title under any patent, copyright, or mask work
 * right to the product. ZX reserves the right to make changes in the
 * software without notification.  ZX also make no representation or
 * warranty that such application will be suitable for the specified use
 * without further testing or modification.
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "osi_api.h"
#include "drv_serial.h"
#include "framework_log.h"

static drvSerial_t *serial_dev = NULL;
static int log_factory_mode = 0;
int vprintf(const char *format, va_list args)
{
    if (serial_dev == NULL) {
        drvSerialCfg_t cfg = {0};
        cfg.tx_buf_size = 64;
        cfg.rx_buf_size = 64;
        cfg.event_mask = DRV_SERIAL_EVENT_RX_ARRIVED;
        cfg.param = 0;
        cfg.event_cb = NULL;
        serial_dev = drvSerialOpen(USERIAL_PORT_EXT_AT, &cfg);
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0) {
        return -1;
    }

    char *buf = (char *)malloc(len + 3);

    if (buf == NULL) {
        return -1;
    }

    vsnprintf(buf, len + 1, format, args);
    strcat(buf, "\r\n");
    len += 2; 
    if(log_factory_mode){
        RTI_LOG("%s", buf);
    }else{
        drvSerialWriteDirect(serial_dev, buf, len);
    }
    free(buf);
    return len;
}
 int printf(const char *format, ...)
 {
     va_list args;
     va_start(args, format);
     int result = vprintf(format, args);
     va_end(args);
     return result;
 }

 
 
// void _write(void){}
// void _close(void){}
// void _fstat(void){}
// void _isatty(void){}
// void _read(void){}
// void _lseek(void){}

void framework_log_buf_dump(const char *tag, const void *buffer, size_t length)
{
    const unsigned char *data = (const unsigned char *)buffer;
    const char *label = (tag != NULL) ? tag : "BUF";

    if (data == NULL || length == 0) {
        FRAMEWORK_LOG("[%s] dump empty", label);
        return;
    }

    char line[80];
    for (size_t i = 0; i < length; i += 16) {
        size_t chunk = (length - i > 16) ? 16 : (length - i);
        int pos = snprintf(line, sizeof(line), "%08X: ", (unsigned int)i);
        for (size_t j = 0; j < 16; ++j) {
            if (j < chunk) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", data[i + j]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
        }
        pos += snprintf(line + pos, sizeof(line) - pos, "| ");
        for (size_t j = 0; j < chunk; ++j) {
            unsigned char c = data[i + j];
            line[pos++] = (c >= 32 && c < 127) ? (char)c : '.';
            if (pos >= (int)sizeof(line) - 2) {
                break;
            }
        }
        line[pos++] = '\0';
        FRAMEWORK_LOG("[%s] %s", label, line);
    }
}

void framework_log_factory_mode(int enbale)
{
    if (enbale) {
        log_factory_mode = 1;
        FRAMEWORK_LOG("Factory mode log enable\n");
    } else {
        log_factory_mode = 0;
        FRAMEWORK_LOG("Factory mode log disable\n");
    }
}

void log_buffer_in_chunks_with_label(const char *tag, const char *label, const char *data, size_t length)
{
    (void)tag;
    const char *use_label = (label != NULL && label[0] != '\0') ? label : "CHUNK";

    if (data == NULL)
    {
        FRAMEWORK_LOG("[WARN][%s] NULL message\n", use_label);
        return;
    }

    if (length == 0)
    {
        FRAMEWORK_LOG("[INFO][%s] Empty message (len=0)\n", use_label);
        return;
    }

    size_t total_chunks = (length + LOG_CHUNK_SIZE - 1) / LOG_CHUNK_SIZE;
    size_t offset = 0;
    size_t chunk_idx = 0;

    char chunk_buf[LOG_CHUNK_SIZE + 1];

    FRAMEWORK_LOG("[INFO][%s] len=%u (%u chunks):\n", use_label, (unsigned int)length, (unsigned int)total_chunks);

    while (offset < length)
    {
        size_t chunk_len = length - offset;
        if (chunk_len > LOG_CHUNK_SIZE)
        {
            chunk_len = LOG_CHUNK_SIZE;
        }

        memcpy(chunk_buf, data + offset, chunk_len);
        chunk_buf[chunk_len] = '\0';

        FRAMEWORK_LOG("[%s %u/%u] %s\n",
                      use_label,
                      (unsigned int)(chunk_idx + 1),
                      (unsigned int)total_chunks,
                      chunk_buf);

        offset += chunk_len;
        chunk_idx++;
    }
}

void log_string_in_chunks_with_label(const char *tag, const char *label, const char *message)
{
    if (message == NULL)
    {
        const char *use_tag = (tag != NULL) ? tag : "LOG";
        const char *use_label = (label != NULL && label[0] != '\0') ? label : "CHUNK";
        OSI_LOGE(use_tag, "[%s] Cannot print NULL log message", use_label);
        return;
    }
    log_buffer_in_chunks_with_label(tag, label, message, strlen(message));
}

void log_string_in_chunks(const char *tag, const char *message)
{
    log_string_in_chunks_with_label(tag, "CHUNK", message);
} 