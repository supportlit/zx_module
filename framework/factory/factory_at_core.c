/**
 * @file factory_at_core.c
 * @brief Factory mode AT command core implementation
 */

#include "factory_at.h"
#include "framework_log.h"
#include "osi_api.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "at_api.h"

#define TAG "FACTORY_AT"

/* AT command list and count */
static uint32_t g_at_cmd_num = 0;
static at_cmd_function_t *g_at_cmd_list = NULL;

/* AT command processing state */
static at_state_t g_at_state = AT_STATE_IDLE;
static int8_t g_at_cmd_line[4 * 1024 + 128];
static int8_t g_at_cmd_res[4 * 1024 + 128];
static uint32_t g_at_data_len = 0;
static at_data_recv_handler_t g_at_data_recv_func = NULL;
static uint32_t g_at_data_recv_timeout = 0;
static uint8_t g_at_cmd_len_list[256];

/* Extra command handler */
static at_extra_cmd_handler_t g_extra_cmd_handler = NULL;

/* Timeout timer */
static osiTimer_t *g_timeout_timer = NULL;

int factory_kernel_at_process(const int8_t *data, uint32_t len)
{
    char *buf = malloc(512+4);
    memset(buf, 0, 512+4);
    char *cmd = malloc(strlen((const char *)data) + 1 + 8);
    sprintf(cmd, "%s\r\n", data);
    int ret = atCmdSendWaitResp(cmd, 1, NULL, 0, "ERROR", buf, 512);
    free(cmd);
    if(ret == 0){
        if(strcmp(buf, "OK") != 0){
            strcat(buf, "\r\n");
            factory_at_response_user((const int8_t *)buf, strlen(buf));
        }
        free(buf);
        return AT_RET_OK;
    }
    free(buf);
    return AT_RET_ERROR;
}

/**
 * @brief Data receive timeout callback
 */
static void prv_recv_timeout_func(void *param)
{
    osiTimerStop(g_timeout_timer);
    OSI_LOGI(TAG, "Data receive timeout");
    
    if (g_at_data_recv_timeout == 0) {
        return;
    }
    
    if (g_at_data_recv_func) {
        at_ret_t ret = g_at_data_recv_func(NULL, 0);
        if (ret == AT_RET_OK) {
            factory_at_response("OK\r\n");
        } else {
            factory_at_response("+CME ERROR: %d\r\n", -ret);
        }
    }
    
    g_at_state = AT_STATE_IDLE;
    g_at_data_recv_timeout = 0;
}

/**
 * @brief Start data receive timer
 */
static void prv_start_recv_data_timer(void)
{
    OSI_LOGI(TAG, "Start recv timer: %d ms", g_at_data_recv_timeout);
    osiTimerStart(g_timeout_timer, g_at_data_recv_timeout);
}

/**
 * @brief Stop data receive timer
 */
static void prv_stop_recv_data_timer(void)
{
    osiTimerStop(g_timeout_timer);
    g_at_data_recv_timeout = 0;
}

/**
 * @brief Search for AT command by name
 * @param cmd_len Command name length
 * @param cmd Command name string
 * @return Command index, or -1 if not found
 */
static int32_t prv_at_cmd_search(uint32_t cmd_len, const int8_t *cmd)
{
    if ((g_at_cmd_list == NULL) || (g_at_cmd_num == 0)) {
        return -1;
    }
    
    for (uint32_t i = 0; i < g_at_cmd_num; i++) {
        if (cmd_len == g_at_cmd_len_list[i]) {
            if (cmd_len == 0) {
                return i;
            } else {
                if (memcmp(cmd, g_at_cmd_list[i].cmd_name, cmd_len) == 0) {
                    return i;
                }
            }
        }
    }
    
    return -1;
}

/**
 * @brief Get AT command name length
 * @param cmd Command string
 * @return Command name length, or -1 on error
 */
static int32_t prv_get_cmd_len(const int8_t *cmd)
{
    uint32_t n = 0;
    
    while (*cmd != '\0') {
        if ((*cmd == 0) || (*cmd == AT_CMD_EOF) || (*cmd == '=') || (*cmd == '?')) {
            return n;
        } else {
            cmd++;
            n++;
        }
    }
    
    return n;
}

/**
 * @brief Process AT command
 * @param cmd_data AT command data
 * @return at_ret_t
 */
static at_ret_t prv_at_cmd_process(int8_t *cmd_data)
{
    int32_t cmd_id = -1;
    int32_t cmd_len;
    at_ret_t ret = AT_RET_ERROR;
    
    OSI_LOGI(TAG, "Process AT command: %s", cmd_data);
    
    cmd_len = prv_get_cmd_len(cmd_data);
    if (cmd_len >= 0) {
        cmd_id = prv_at_cmd_search(cmd_len, cmd_data);
    }
    
    OSI_LOGI(TAG, "Command length: %d, ID: %d", cmd_len, cmd_id);
    
    if (cmd_id >= 0) {
        cmd_data += cmd_len;
        
        if (cmd_data[0] == AT_CMD_EOF) {
            /* Execute command: AT+CMD */
            if (g_at_cmd_list[cmd_id].exe_cmd) {
                OSI_LOGI(TAG, "Execute command");
                ret = g_at_cmd_list[cmd_id].exe_cmd();
            }
        } else if ((cmd_data[0] == '?') && (cmd_data[1] == AT_CMD_EOF)) {
            /* Query command: AT+CMD? */
            if (g_at_cmd_list[cmd_id].query_cmd) {
                OSI_LOGI(TAG, "Query command");
                ret = g_at_cmd_list[cmd_id].query_cmd();
            }
        } else if ((cmd_data[0] == '=') && (cmd_data[1] == '?') && (cmd_data[2] == AT_CMD_EOF)) {
            /* Test command: AT+CMD=? */
            if (g_at_cmd_list[cmd_id].test_cmd) {
                OSI_LOGI(TAG, "Test command");
                ret = g_at_cmd_list[cmd_id].test_cmd();
            }
        } else {
            /* Setup command: AT+CMD=... or AT+CMD0=... */
            if (g_at_cmd_list[cmd_id].setup_cmd) {
                if (cmd_data[0] == '=') {
                    if (cmd_data[1] != AT_CMD_EOF) {
                        OSI_LOGI(TAG, "Setup command");
                        ret = g_at_cmd_list[cmd_id].setup_cmd(0, &cmd_data[1]);
                    }
                } else if ((cmd_data[0] >= '0') && (cmd_data[0] <= '9') && (cmd_data[1] == '=')) {
                    if (cmd_data[2] != AT_CMD_EOF) {
                        uint8_t id = cmd_data[0] - '0';
                        OSI_LOGI(TAG, "Setup command with ID: %d", id);
                        ret = g_at_cmd_list[cmd_id].setup_cmd(id, &cmd_data[2]);
                    }
                }
            }
        }
    } else {
        /* Command not found, try extra handler */
        if (g_extra_cmd_handler) {
            ret = g_extra_cmd_handler(cmd_data, strlen((char *)cmd_data));
        }
    }
    
    OSI_LOGI(TAG, "Process result: %d", ret);
    
    if (ret == AT_RET_WAIT_DATA) {
        if ((g_at_data_len == 0) || (g_at_data_len > sizeof(g_at_cmd_line)) || (g_at_data_recv_func == NULL)) {
            ret = AT_RET_ERROR;
        }
    }
    
    /* Send response */
    if (ret == AT_RET_OK) {
        factory_at_response("OK\r\n");
    } else if (ret == AT_RET_WAIT_DATA) {
        factory_at_response(">\r\n");
    } else if (ret == AT_RET_ERROR) {
        factory_at_response("ERROR\r\n");
    } else {
        factory_at_response("+CME ERROR: %d\r\n", -ret);
    }
    
    return ret;
}

/**
 * @brief Initialize AT command system
 */
int factory_at_init(at_cmd_function_t *cmd_list, uint32_t cmd_count)
{
    if (cmd_count > sizeof(g_at_cmd_len_list) / sizeof(g_at_cmd_len_list[0])) {
        return -1;
    }
    
    g_at_cmd_list = cmd_list;
    g_at_cmd_num = cmd_count;
    
    /* Calculate command name lengths */
    for (uint32_t i = 0; i < cmd_count; i++) {
        if (cmd_list[i].cmd_name) {
            g_at_cmd_len_list[i] = strlen(cmd_list[i].cmd_name);
        } else {
            g_at_cmd_len_list[i] = 0;
        }
    }
    
    /* Create timeout timer */
    g_timeout_timer = osiTimerCreate(prv_recv_timeout_func, NULL);
    if (g_timeout_timer == NULL) {
        OSI_LOGE(TAG, "Failed to create timeout timer");
        return -1;
    }
    g_extra_cmd_handler = (at_extra_cmd_handler_t)factory_kernel_at_process;
    /* Call user-specific initialization */
    return factory_at_init_user();
}

/**
 * @brief Process received AT command data
 */
void factory_at_process(const int8_t *data, uint32_t len)
{
    static int8_t *cmd_line_ptr = g_at_cmd_line;
    static uint32_t data_len = 0;
    int8_t temp;
    uint32_t i = 0;
    
    while (i < len) {
        temp = data[i];
        i++;
        
        switch (g_at_state) {
        case AT_STATE_IDLE:
            /* Search for "AT" header */
            g_at_cmd_line[0] = g_at_cmd_line[1];
            g_at_cmd_line[1] = temp;
            
            if ((memcmp(g_at_cmd_line, "AT", 2) == 0) || (memcmp(g_at_cmd_line, "at", 2) == 0)) {
                cmd_line_ptr = g_at_cmd_line + 2;
                g_at_state = AT_STATE_CMD_RECVING;
            }
            break;
            
        case AT_STATE_CMD_RECVING:
            /* Receive command line */
            *cmd_line_ptr = temp;
            cmd_line_ptr++;
            
            if (cmd_line_ptr > &g_at_cmd_line[sizeof(g_at_cmd_line) - 1]) {
                g_at_state = AT_STATE_IDLE;
            } else {
                if (temp == '\n') {
                    *cmd_line_ptr = '\0';
                    
                    /* Remove \n */
                    cmd_line_ptr--;
                    *cmd_line_ptr = '\0';
                    
                    g_at_state = AT_STATE_CMD_PROCESS;
                    if (prv_at_cmd_process(g_at_cmd_line) == AT_RET_WAIT_DATA) {
                        cmd_line_ptr = g_at_cmd_line;
                        data_len = 0;
                        g_at_state = AT_STATE_DATA_RECVING;
                        prv_start_recv_data_timer();
                    } else {
                        g_at_state = AT_STATE_IDLE;
                    }
                    memset(g_at_cmd_line, 0, sizeof(g_at_cmd_line));
                }
            }
            break;
            
        case AT_STATE_DATA_RECVING:
            /* Receive data */
            *cmd_line_ptr = temp;
            cmd_line_ptr++;
            data_len++;
            
            if (data_len == g_at_data_len) {
                if (g_at_data_recv_func) {
                    prv_stop_recv_data_timer();
                    at_ret_t ret = g_at_data_recv_func(g_at_cmd_line, g_at_data_len);
                    if (ret == AT_RET_OK) {
                        factory_at_response("OK\r\n");
                    } else {
                        factory_at_response("+CME ERROR: %d\r\n", ret);
                    }
                }
                
                g_at_state = AT_STATE_IDLE;
                memset(g_at_cmd_line, 0, sizeof(g_at_cmd_line));
            }
            break;
            
        case AT_STATE_CMD_PROCESS:
            if (temp == '\n') {
                factory_at_response("+BUSY\r\n");
            }
            break;
            
        case AT_STATE_DATA_WAIT_SEND:
            if (temp == '>') {
                g_at_state = AT_STATE_DATA_SENDING;
            }
            break;
            
        case AT_STATE_DATA_SENDING:
            if (temp == '\n') {
                factory_at_response("+CME ERROR: 3803\r\n");
            }
            break;
            
        default:
            break;
        }
    }
}

/**
 * @brief Wait for data reception
 */
int factory_at_wait_data(uint32_t len, at_data_recv_handler_t callback, uint32_t timeout_sec)
{
    if ((len == 0) || (len > sizeof(g_at_cmd_line)) || (callback == NULL)) {
        return -1;
    }
    
    g_at_data_len = len;
    g_at_data_recv_func = callback;
    
    if (timeout_sec == 0) {
        g_at_data_recv_timeout = RECV_DATA_TIMEOUT_DEFAULT;
    } else {
        g_at_data_recv_timeout = timeout_sec * 1000;
    }
    
    return 0;
}

/**
 * @brief Send AT command response
 */
int factory_at_response(const char *fmt, ...)
{
    va_list args;
    int32_t rc;
    
    va_start(args, fmt);
    memset(g_at_cmd_res, 0, sizeof(g_at_cmd_res));
    rc = vsnprintf((char *)g_at_cmd_res, sizeof(g_at_cmd_res), fmt, args);
    va_end(args);
    
    if (rc > 0) {
        return factory_at_response_user(g_at_cmd_res, rc);
    }
    
    return -1;
}

/**
 * @brief Send binary data
 */
void factory_at_send_data(const int8_t *data, uint32_t len)
{
    while (g_at_state != AT_STATE_IDLE) {
        osiThreadSleep(5);
    }
    
    if (factory_at_response_user(data, len) != 0) {
        goto exit;
    }
    
exit:
    g_at_state = AT_STATE_IDLE;
}

/**
 * @brief Register extra command handler
 */
void factory_at_register_extra_handler(at_extra_cmd_handler_t handler)
{
    g_extra_cmd_handler = handler;
}
