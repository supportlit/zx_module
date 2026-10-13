/**
 * @file factory_at_cmds.c
 * @brief Factory mode AT command implementations
 */

#include "factory_at_cmds.h"
#include "factory_at.h"
#include "framework_log.h"
#include "framework_led.h"
#include "framework_key.h"
#include "framework_audio.h"
#include "framework_battery.h"
#include "framework_device_config.h"
#include "osi_api.h"
#include "flash_api.h"
#include "drv_serial.h"
#include "fs_api.h"
#include "wifi.h"
#include "lwip/sockets.h"
#include "ipv4/inet.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "app_version.h"

#define TAG "FACTORY_CMD"

/* Factory test event flags */
#define TEST_SUCCESS          (0x01 << 0)
#define TEST_FAILED           (0x01 << 1)
#define TEST_FILE_PLAY_START  (0x01 << 2)
#define TEST_FILE_PLAY_END    (0x01 << 3)

/* Serial device for AT commands */
static drvSerial_t *g_at_serial = NULL;
static osiFlag_t *g_factory_flag = NULL;

factory_at_cmd_handlers_t *g_factory_at_handlers = NULL;

/* Helper function: convert string to integer */
static int str_to_int(const char *str)
{
    int ret = 0;
    int dir = 1;
    const char *tmp = str;
    
    if (tmp == NULL) return 0;
    
    if (*tmp == '-') {
        dir = -1;
        tmp++;
    } else if (*tmp == '+') {
        dir = 1;
        tmp++;
    }
    
    while ((*tmp >= '0') && (*tmp <= '9')) {
        ret = ret * 10 + (*tmp - '0');
        tmp++;
    }
    
    return ret * dir;
}

/* ============================================================================
 * Serial initialization
 * ============================================================================ */

static void prv_serial_event_cb(drvSerialEvent_t event, unsigned long param)
{
    char *buf = malloc(512);
    int rlen = 0;
    
    if (event == DRV_SERIAL_EVENT_RX_ARRIVED) {
        memset(buf, 0, 512);
        rlen = drvSerialRead(g_at_serial, buf, sizeof(buf));
        while (rlen > 0) {
            OSI_LOGI(TAG, "Recv: %d bytes", rlen);
            factory_at_process((int8_t*)buf, rlen);
            rlen = drvSerialRead(g_at_serial, buf, sizeof(buf));
        }
    }
}

int factory_at_init_user(void)
{
    drvSerialCfg_t cfg;
    
    cfg.event_cb = prv_serial_event_cb;
    cfg.tx_buf_size = 4 * 1024;
    cfg.rx_buf_size = 4 * 1024;
    
    g_at_serial = drvSerialOpen(USERIAL_PORT_AT, &cfg);
    if (g_at_serial == NULL) {
        OSI_LOGE(TAG, "Failed to open serial device");
        return -1;
    }
    
    OSI_LOGI(TAG, "AT serial initialized");
    return 0;
}

int factory_at_response_user(const int8_t *data, uint32_t len)
{
    if (g_at_serial == NULL || data == NULL || len <= 0) {
        return -1;
    }
    
    drvSerialWriteDirect(g_at_serial, data, len);
    return 0;
}

/* ============================================================================
 * System commands
 * ============================================================================ */

at_ret_t at_cmd_firmware(void)
{
    factory_at_response("+FIRMWARE: " APP_VERSION_STRING "\r\n");
    return AT_RET_OK;
}

at_ret_t at_cmd_restart(void)
{
    factory_at_response("Restarting...\r\n");
    osiThreadSleep(500);
    osiSysReset();
    return AT_RET_OK;
}

/* ============================================================================
 * LED control commands
 * ============================================================================ */

at_ret_t at_cmd_ctl_red(void)
{
    #ifdef FRAMEWORK_ENABLE_LED
    framework_led_set_state(LED_STATE_EXCEPTION_POWER);
    #endif
    /* TODO: Set specific red LED pattern */
    return AT_RET_OK;
}

at_ret_t at_cmd_ctl_green(void)
{
    #ifdef FRAMEWORK_ENABLE_LED
    framework_led_set_state(LED_STATE_NET_CONNECTED);
    #endif
    /* TODO: Set specific green LED pattern */
    return AT_RET_OK;
}

at_ret_t at_cmd_ctl_blue(void)
{
    #ifdef FRAMEWORK_ENABLE_LED
    framework_led_set_state(LED_STATE_CHARGING);
    #endif
    /* TODO: Set specific blue LED pattern */
    return AT_RET_OK;
}

at_ret_t at_cmd_ctl_off(void)
{
    #ifdef FRAMEWORK_ENABLE_LED
    framework_led_clear_all();
    #endif
    return AT_RET_OK;
}

at_ret_t at_cmd_led_test(void)
{
    OSI_LOGI(TAG, "LED test command");
    // if (g_factory_at_handlers && g_factory_at_handlers->led_handler) {
    //     return g_factory_at_handlers->led_handler(NULL);
    // }
    
    return AT_RET_OK;
}

/* ============================================================================
 * Key test command
 * ============================================================================ */

static void prv_key_test_callback(powkey_event_e event, void *param)
{
    static uint8_t key_pressed[3] = {0}; /* POWER, VOL_UP, VOL_DOWN */
    
    if (g_factory_flag == NULL) return;
    
    /* Track which keys have been pressed */
    switch (event) {
    case POWKEY_EVENT_PRESS:
        key_pressed[0] = 1;
        break;
    case POWKEY_EVENT_LONG_PRESS:
        key_pressed[0] = 1;
        break;
    default:
        break;
    }
    
    if (key_pressed[0]) {
        memset(key_pressed, 0, sizeof(key_pressed));
        osiFlagSet(g_factory_flag, TEST_SUCCESS, OSI_FLAG_OR);
    }
}

at_ret_t at_cmd_keytest(void)
{
    uint32_t event;
    int ret;

    if (g_factory_at_handlers && g_factory_at_handlers->key_handler) {
        return g_factory_at_handlers->key_handler(NULL);
    }
    
    g_factory_flag = osiFlagCreate();
    if (g_factory_flag == NULL) {
        return AT_RET_ERROR;
    }
    
    /* Register key callback */
    framework_key_register_pwk_callback(prv_key_test_callback, NULL);
    
    /* Wait for all keys to be pressed (10 seconds timeout) */
    ret = osiFlagWait(g_factory_flag, TEST_SUCCESS | TEST_FAILED, 
                      OSI_FLAG_OR, &event, 10 * 1000);
    
    /* Unregister callback */
    framework_key_register_pwk_callback(NULL, NULL);
    
    osiFlagDelete(g_factory_flag);
    g_factory_flag = NULL;
    
    if ((ret == 0) && (event & TEST_SUCCESS)) {
        factory_at_response("key_test_ok\r\n");
        return AT_RET_OK;
    }
    
    factory_at_response("key_test_fail\r\n");
    return AT_RET_ERROR;
}

/* ============================================================================
 * Audio commands
 * ============================================================================ */

at_ret_t at_cmd_spk_play(uint8_t id, int8_t *param)
{
    char *filename;
    
    filename = strtok((char *)param, "\r\n");
    if (filename == NULL) {
        return AT_RET_ERROR;
    }
    
    OSI_LOGI(TAG, "Play audio: %s", filename);
    
    /* Check if file exists */
    if (fs_size(filename) <= 0) {
        factory_at_response("File not found\r\n");
        return AT_RET_ERROR;
    }
    
    /* Play audio file using framework audio module */
    int ret = audio_resource_play_custom(filename, NULL);
    if (ret < 0) {
        return AT_RET_ERROR;
    }
    
    return AT_RET_OK;
}

at_ret_t at_cmd_audio_test(uint8_t id, int8_t *param)
{
    /* Play audio file using framework audio module */
    int ret = audio_file_play("D:/factory_mode.mp3",NULL);
    if (ret < 0) {
        return AT_RET_ERROR;
    }
    
    return AT_RET_OK;
}

at_ret_t at_cmd_spk_stop(void)
{
    /* Stop audio playback */
    audio_resource_stop();
    return AT_RET_OK;
}

/* ============================================================================
 * File system commands
 * ============================================================================ */

typedef struct {
    char filename[128];
    int32_t fd;
    int32_t offset;
    int32_t size;
} file_write_ctx_t;

static file_write_ctx_t g_file_ctx;

static at_ret_t prv_file_write_handler(const int8_t *data, uint32_t len)
{
    uint32_t wlen = 0;
    
    if (data == NULL || len <= 0) {
        OSI_LOGE(TAG, "File write: invalid data");
        return -2;
    }
    
    if (g_file_ctx.size != len) {
        OSI_LOGE(TAG, "File write: size mismatch");
        return -3;
    }
    
    /* Open file */
    if (g_file_ctx.offset == 0) {
        g_file_ctx.fd = fs_open(g_file_ctx.filename, "wb+");
    } else {
        g_file_ctx.fd = fs_open(g_file_ctx.filename, "r+b");
    }
    
    if (g_file_ctx.fd < 0) {
        OSI_LOGE(TAG, "File write: failed to open file");
        return -4;
    }
    
    /* Seek and write */
    fs_seek(g_file_ctx.fd, g_file_ctx.offset, SEEK_SET);
    wlen = fs_write(data, 1, len, g_file_ctx.fd);
    g_file_ctx.offset += wlen;
    fs_close(g_file_ctx.fd);
    
    OSI_LOGI(TAG, "Wrote %d bytes, total offset: %d", wlen, g_file_ctx.offset);
    
    if (wlen != len) {
        return -5;
    }
    
    return AT_RET_OK;
}

at_ret_t at_cmd_fs_download(uint8_t id, int8_t *param)
{
    char *filename;
    char *size_str;
    char *offset_str;
    
    memset(&g_file_ctx, 0, sizeof(g_file_ctx));
    
    filename = strtok((char *)param, ",");
    if (filename) {
        strncpy(g_file_ctx.filename, filename, sizeof(g_file_ctx.filename) - 1);
        OSI_LOGI(TAG, "Download file: %s", g_file_ctx.filename);
    } else {
        return AT_RET_ERROR;
    }
    
    size_str = strtok(NULL, ",");
    if (size_str) {
        g_file_ctx.size = str_to_int(size_str);
        OSI_LOGI(TAG, "Size: %d", g_file_ctx.size);
    } else {
        return AT_RET_ERROR;
    }
    
    offset_str = strtok(NULL, ",");
    if (offset_str) {
        g_file_ctx.offset = str_to_int(offset_str);
        OSI_LOGI(TAG, "Offset: %d", g_file_ctx.offset);
    } else {
        g_file_ctx.offset = 0;
    }
    
    if (g_file_ctx.size <= 0 || g_file_ctx.offset < 0) {
        return AT_RET_ERROR;
    }
    
    if (factory_at_wait_data(g_file_ctx.size, prv_file_write_handler, 0) != 0) {
        return AT_RET_ERROR;
    }
    
    return AT_RET_WAIT_DATA;
}

#define READ_PACK_SIZE (4 * 1024)

at_ret_t at_cmd_fs_read(uint8_t id, int8_t *param)
{
    char *filename;
    int32_t fd;
    int32_t file_size;
    char *read_buf = NULL;
    int wlen;
    
    filename = strtok((char *)param, "\r\n");
    if (filename == NULL) {
        return AT_RET_ERROR;
    }
    
    OSI_LOGI(TAG, "Read file: %s", filename);
    
    file_size = fs_size(filename);
    if (file_size <= 0) {
        factory_at_response("+FSRDFILE: %s,0,\r\n", filename);
        return AT_RET_OK;
    }
    
    fd = fs_open(filename, "rb");
    if (fd < 0) {
        OSI_LOGE(TAG, "Failed to open file");
        return AT_RET_ERROR;
    }
    
    read_buf = malloc(READ_PACK_SIZE);
    if (read_buf == NULL) {
        fs_close(fd);
        return AT_RET_ERROR;
    }
    
    /* Send header */
    wlen = sprintf(read_buf, "+FSRDFILE: %s,%ld,", filename, (long)file_size);
    factory_at_response_user((int8_t *)read_buf, wlen);
    
    /* Send file data in chunks */
    while (1) {
        wlen = fs_read(read_buf, 1, READ_PACK_SIZE, fd);
        if (wlen > 0) {
            factory_at_response_user((int8_t *)read_buf, wlen);
            osiThreadSleep(100);
        } else {
            break;
        }
    }
    
    factory_at_response_user((int8_t *)"\r\n", 2);
    
    fs_close(fd);
    free(read_buf);
    
    return AT_RET_OK;
}

at_ret_t at_cmd_fs_delete(void)
{
    fs_remove("1k_0db.mp3");
    factory_at_response("Deleted noise file\r\n");
    
    fs_remove("1kHz_0dB_5s.mp3");
    factory_at_response("Deleted wave file\r\n");
    
    fs_remove("play_date.txt");
    factory_at_response("Deleted date file\r\n");
    
    return AT_RET_OK;
}

/* ============================================================================
 * Device SN commands
 * ============================================================================ */

#define DEVICE_SN_PATH "D:/user_cfg_bak.txt"
#define USER_CFG_PTABLE         "user_cfg"
at_ret_t at_cmd_set_sn(uint8_t id, int8_t *param)
{
    char *sn;
    int32_t fd;
    char *json_str = NULL;
    int wlen;
    size_t config_size = 0;
    char *processed_sn = NULL;
    
    sn = strtok((char *)param, "\r\n");
    if (sn == NULL) {
        return AT_RET_ERROR;
    }
    // OSI_LOGI(TAG, "Set SN: %s", sn);

    processed_sn = malloc(strlen(sn) + 1);
    if (processed_sn == NULL) {
        return AT_RET_ERROR;
    }
    
    // 去除可能的引号
    if (sn[0] == '"' && sn[strlen(sn) - 1] == '"') {
        // 去掉首尾引号
        strncpy(processed_sn, sn + 1, strlen(sn) - 2);
        processed_sn[strlen(sn) - 2] = '\0';
    } else {
        strcpy(processed_sn, sn);
    }
    OSI_LOGI(TAG, "Set SN: %s", processed_sn);
    config_size = flash_read_size(USER_CFG_PTABLE);
    if (config_size == 0) {
        OSI_LOGE(TAG, "device_config: Failed to get partition size");
        return AT_RET_ERROR;
    }
    
    json_str = malloc(config_size+1);
    if (json_str == NULL) {
        return AT_RET_ERROR;
    }
    memset(json_str, 0xFF, config_size);

    framework_mqtt_config_t *g_mqtt_config = NULL;
    g_mqtt_config = (framework_mqtt_config_t*)json_str;
    memset(g_mqtt_config, 0, sizeof(framework_mqtt_config_t));
    g_mqtt_config->magic = 0x46524D43;
    sniprintf(g_mqtt_config->device_sn, sizeof(g_mqtt_config->device_sn), "%s", processed_sn);
    strcpy(g_mqtt_config->device_name, "SOUNDBOX");
    
    free(processed_sn);  // 释放临时字符串
    fd = fs_open(DEVICE_SN_PATH, "wb+");
    if (fd < 0) {
        free(json_str);
        return AT_RET_ERROR;
    }
    
    wlen = fs_write(json_str, sizeof(uint8_t), config_size, fd);
    fs_close(fd);
    
    if (wlen != config_size) {
        if(json_str)free(json_str);
        return AT_RET_ERROR;
    }
    if(json_str)free(json_str);
    return AT_RET_OK;
}

at_ret_t at_cmd_get_sn(void)
{
    int32_t fd;
    int32_t file_size;
    char *buf = NULL;
    int res;
    
    file_size = fs_size(DEVICE_SN_PATH);
    if (file_size <= 0) {
        factory_at_response("SN file not found\r\n");
        return AT_RET_ERROR;
    }
    
    buf = malloc(file_size + 1);
    if (buf == NULL) {
        return AT_RET_ERROR;
    }
    
    memset(buf, 0, file_size + 1);
    
    fd = fs_open(DEVICE_SN_PATH, "rb");
    if (fd < 0) {
        free(buf);
        return AT_RET_ERROR;
    }
    
    res = fs_read(buf, 1, file_size, fd);
    fs_close(fd);
    
    if (res != file_size) {
        free(buf);
        return AT_RET_ERROR;
    }
    
    /* Parse JSON to get device_name */
    /* TODO: Use proper JSON parser */
    factory_at_response("\"device_sn\":%s\r\n", buf);
    
    free(buf);
    return AT_RET_OK;
}

/* ============================================================================
 * WiFi test commands
 * ============================================================================ */

#define WIFI_TCPTEST_HOST    "120.79.88.194"
#define WIFI_TCPTEST_PORT    6019
#define WIFI_TCPTEST_BUFF    "ZxInfoTek WIFI TEST!"

static int wifi_tcp_test(void)
{
    int fd = -1;
    int ret;
    char *read_buf = NULL;
    struct sockaddr_in server;
    struct timeval tv = {2, 0}; /* 2s timeout */

    read_buf = malloc(128);
    if (read_buf == NULL) {
        OSI_LOGE(TAG, "wifi_tcp_test malloc fail");
        return -1;
    }
    memset(read_buf, 0, 128);

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        OSI_LOGE(TAG, "wifi_tcp_test socket fail: %d", fd);
        free(read_buf);
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(WIFI_TCPTEST_PORT);
    server.sin_addr.s_addr = inet_addr(WIFI_TCPTEST_HOST);

    ret = connect(fd, (struct sockaddr *)&server, sizeof(server));
    if (ret != 0) {
        OSI_LOGE(TAG, "wifi_tcp_test connect err: %d", ret);
        goto task_err;
    }

    ret = send(fd, WIFI_TCPTEST_BUFF, strlen(WIFI_TCPTEST_BUFF), 0);
    if (ret != (int)strlen(WIFI_TCPTEST_BUFF)) {
        OSI_LOGE(TAG, "wifi_tcp_test write err: %d", ret);
        goto task_err;
    }

    ret = recv(fd, read_buf, strlen(WIFI_TCPTEST_BUFF), 0);
    if (ret != (int)strlen(WIFI_TCPTEST_BUFF)) {
        OSI_LOGE(TAG, "wifi_tcp_test recv err: %d", ret);
        goto task_err;
    }

    OSI_LOGI(TAG, "wifi_tcp_test buf: %s", read_buf);
    if (strncmp(read_buf, WIFI_TCPTEST_BUFF, strlen(WIFI_TCPTEST_BUFF)) == 0) {
        closesocket(fd);
        free(read_buf);
        return 0;
    }

task_err:
    if (fd >= 0) {
        closesocket(fd);
    }
    free(read_buf);
    return -1;
}

/* Remove surrounding quotes and trailing CR/LF/space in-place */
static void prv_trim_param(char *str)
{
    if (str == NULL) return;

    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' ||
                       str[len - 1] == ' '  || str[len - 1] == '"')) {
        str[--len] = '\0';
    }

    while (len > 0 && (str[0] == ' ' || str[0] == '"')) {
        memmove(str, str + 1, len);
        len--;
    }
}

static wifi_join_status prv_wait_wifi_status(uint32_t timeout_ms)
{
    const uint32_t step_ms = 200;
    uint32_t elapsed = 0;
    wifi_join_status status = wifi_get_join_status();

    while (status != WIFI_JOIN_CONNECTED && elapsed < timeout_ms) {
        osiThreadSleep(step_ms/5);
        elapsed += step_ms;
        status = wifi_get_join_status();
    }

    return status;
}

at_ret_t at_cmd_wifi_test(uint8_t id, int8_t *param)
{
    (void)id;
    char *ssid_raw = NULL;
    char *pwd_raw = NULL;
    char ssid[64] = {0};
    char pwd[128] = {0};
    wifi_join_status status;
    network_info_t info;
    char ip_str[32] = {0};

    if (param == NULL) {
        return AT_RET_ERROR;
    }

    ssid_raw = strtok((char *)param, ",");
    pwd_raw = strtok(NULL, "\r\n");

    if (ssid_raw == NULL) {
        factory_at_response("+WIFITEST:INVALID_PARAM\r\n");
        return AT_RET_ERROR;
    }

    if (pwd_raw == NULL) {
        pwd_raw = "";
    }

    prv_trim_param(ssid_raw);
    prv_trim_param(pwd_raw);

    if (strlen(ssid_raw) == 0) {
        factory_at_response("+WIFITEST:INVALID_SSID\r\n");
        return AT_RET_ERROR;
    }

    snprintf(ssid, sizeof(ssid), "%s", ssid_raw);
    snprintf(pwd, sizeof(pwd), "%s", pwd_raw);

    OSI_LOGI(TAG, "WiFi test: ssid=%s", ssid);

    if (wifi_is_open() == 0) {
        if (wifi_open() != 0) {
            factory_at_response("+WIFITEST:OPEN_FAIL\r\n");
            return AT_RET_ERROR;
        }
    }

    wifi_disconnect();

    if (wifi_connect(ssid, pwd) != 0) {
        factory_at_response("+WIFITEST:CONNECT_FAIL\r\n");
        return AT_RET_ERROR;
    }

    status = prv_wait_wifi_status(20000);

    if (status != WIFI_JOIN_CONNECTED) {
        factory_at_response("+WIFITEST:TIMEOUT\r\n");
        return AT_RET_ERROR;
    }

    int wifi_sock_en = 1;
    wifi_set_opt(1, &wifi_sock_en, sizeof(wifi_sock_en));

    if (wifi_tcp_test() != 0) {
        factory_at_response("+WIFITEST:TCP_FAIL\r\n");
        return AT_RET_ERROR;
    }

    if (wifi_get_network_info(&info) == 0) {
        snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                 info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
        factory_at_response("+WIFITEST:OK,%s,%d\r\n", ip_str, info.rssi);
    } else {
        factory_at_response("+WIFITEST:OK\r\n");
    }

    return AT_RET_OK;
}

at_ret_t at_cmd_wifi_csq(void)
{
    int rssi = 0;
    network_info_t info;

    if (wifi_is_open() == 0 || wifi_is_connected() == 0) {
        factory_at_response("+WIFICSQGET:NOT_CONNECTED\r\n");
        return AT_RET_ERROR;
    }

    if (wifi_get_rssi(&rssi) != 0) {
        if (wifi_get_network_info(&info) == 0) {
            rssi = info.rssi;
        } else {
            factory_at_response("+WIFICSQGET:READ_FAIL\r\n");
            return AT_RET_ERROR;
        }
    }

    factory_at_response("\"wifi_csq\":\"%d\"\r\n", rssi);
    return AT_RET_OK;
}

at_ret_t at_cmd_wifi_version(void)
{
    int32_t fd;
    int32_t file_size;
    char *buf = NULL;
    
    file_size = fs_size("D:/wifi_vsn.cfg");
    if (file_size <= 0) {
        factory_at_response("WiFi version file not found\r\n");
        return AT_RET_ERROR;
    }
    
    buf = osiMalloc(file_size + 1);
    if (buf == NULL) {
        return AT_RET_ERROR;
    }
    
    memset(buf, 0, file_size + 1);
    
    fd = fs_open("D:/wifi_vsn.cfg", "rb");
    if (fd < 0) {
        osiFree(buf);
        return AT_RET_ERROR;
    }
    
    fs_read(buf, 1, file_size, fd);
    fs_close(fd);
    
    factory_at_response("\"wifi_version\":\"%s\"\r\n", buf);
    osiFree(buf);
    
    return AT_RET_OK;
}

/* ============================================================================
 * Battery test command
 * ============================================================================ */

at_ret_t at_cmd_battery(void)
{
    int voltage = framework_factory_battery_voltage();
    factory_at_response("Battery: %d mV\r\n", voltage);
    return AT_RET_OK;
}

at_ret_t at_cmd_pt_btn_test(uint8_t id, int8_t *param)
{
    // if (g_factory_at_handlers && g_factory_at_handlers->pt_btn_handler) {
    //     return g_factory_at_handlers->pt_btn_handler(param);
    // }
    return AT_RET_OK;
}

/* ============================================================================
 * AT command table
 * ============================================================================ */

static at_cmd_function_t g_at_cmd_table[] = {
    /* System commands */
    {"AT+FIRMWARE",     NULL, NULL, NULL,                at_cmd_firmware},
    {"AT+RESTART",      NULL, NULL, NULL,                at_cmd_restart},
    
    /* LED control commands */
    {"AT+CTL_R",        NULL, NULL, NULL,                at_cmd_ctl_red},
    {"AT+CTL_G",        NULL, NULL, NULL,                at_cmd_ctl_green},
    {"AT+CTL_B",        NULL, NULL, NULL,                at_cmd_ctl_blue},
    {"AT+CTL_OFF",      NULL, NULL, NULL,                at_cmd_ctl_off},
    {"AT+LEDTEST",      NULL, NULL, NULL,                at_cmd_led_test},
    
    /* Key test */
    {"AT+KEYTEST",      NULL, NULL, NULL,                at_cmd_keytest},
    {"AT+PTBTNTEST",      NULL, NULL, at_cmd_pt_btn_test,                NULL},
    
    /* Audio commands */
    {"AT+SPK",          NULL, NULL, at_cmd_spk_play,     NULL},
    {"AT+SPK_STOP",     NULL, NULL, NULL,                at_cmd_spk_stop},
    
    /* File system commands */
    {"AT+FSDWNFILE",    NULL, NULL, at_cmd_fs_download,  NULL},
    {"AT+FSRDFILE",     NULL, NULL, at_cmd_fs_read,      NULL},
    {"AT+DEL",          NULL, NULL, NULL,                at_cmd_fs_delete},
    
    /* Device SN commands */
    {"AT+SETSN",        NULL, NULL, at_cmd_set_sn,       NULL},
    {"AT+GETSN",        NULL, NULL, NULL,                at_cmd_get_sn},
    
    /* WiFi test commands */
    {"AT+WIFITEST",     NULL, NULL, at_cmd_wifi_test,    NULL},
    {"AT+WIFICSQGET",   NULL, NULL, NULL,                at_cmd_wifi_csq},
    {"AT+WIFIVERSION",  NULL, NULL, NULL,                at_cmd_wifi_version},
    
    /* Battery test */
    {"AT+BATTERY",      NULL, NULL, NULL,                at_cmd_battery},

    {"AT+PTAUDIOTEST",      NULL, NULL, at_cmd_audio_test,                NULL},
};

/**
 * @brief Initialize AT commands
 */
void factory_at_cmds_init(void)
{
    uint32_t cmd_count = sizeof(g_at_cmd_table) / sizeof(g_at_cmd_table[0]);
    
    OSI_LOGI(TAG, "Initializing %d AT commands", cmd_count);
    
    if (factory_at_init(g_at_cmd_table, cmd_count) < 0) {
        OSI_LOGE(TAG, "Failed to initialize AT commands");
    }
}

void factory_at_set_cmd_handlers(factory_at_cmd_handlers_t *handlers)
{
    if (handlers) {
        g_factory_at_handlers = handlers;
    }
}
