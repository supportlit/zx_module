/* ----------------------------------------------------------------------------
 * Copyright (c) Huawei Technologies Co., Ltd. 2013-2021. All rights reserved.
 * Description: Esp8266 At Device HeadFile
 * Author: Huawei LiteOS Team
 * Create: 2013-01-01
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --------------------------------------------------------------------------- */

#ifndef _ESP8266_H
#define _ESP8266_H

#include "at_main.h"

#define MAX_AT_USERDATA_LEN       (1024*8)

#define WIFI_SSID                 "NPWIFITEST" // set ssid name
#define WIFI_PASSWD               "np72206220" // set ssid passwd

#define AT_MODU_NAME              "ESP8266"
#define AT_USART_PORT             3
#define AT_BUARDRATE              115200
#define AT_CMD_TIMEOUT            20000 // ms
#define AT_MAX_LINK_NUM           5

#define AT_LINE_END               "\r\n"
#define AT_CMD_BEGIN              "\r\n"

#define AT_CMD_RST                "AT+RST"
#define AT_CMD_ECHO_OFF           "ATE0"
#define AT_CMD_CWMODE             "AT+CWMODE"
#define AT_CMD_JOINAP             "AT+CWJAP"
#define AT_CMD_QUITAP             "AT+CWQAP"
#define AT_CMD_MUX                "AT+CIPMUX"
#define AT_CMD_SERVER             "AT+CIPSERVER"
#define AT_CMD_CONN               "AT+CIPSTART"
#define AT_CMD_IPSTATES           "AT+CIPSTATUS"
#define AT_CMD_SEND               "AT+CIPSEND"
#define AT_CMD_RECV_MODE          "AT+CIPRECVTYPE"
#define AT_CMD_RECV_MODE2         "AT+CIPRECVMODE"
#define AT_CMD_RECV               "AT+CIPRECVDATA"
#define AT_CMD_CLOSE              "AT+CIPCLOSE"
#define AT_CMD_ALLSTACLOSE        "AT+CWQIF"
#define AT_CMD_CHECK_IP           "AT+CIPSTA?"
#define AT_CMD_CHECK_MAC          "AT+CIPSTAMAC?"
#define AT_CMD_SHOW_DINFO         "AT+CIPDINFO"
#define AT_CMD_SETUP_AUTO_CONN    "AT+CWAUTOCONN"
#define AT_CMD_DNS                "AT+CIPDOMAIN"
#define AT_CMD_AP_IP_CFG          "AT+CIPAP"
#define AT_CMD_AP_DHCP_IP_CFG     "AT+CWDHCPS"
#define AT_CMD_SLEEP              "AT+SLEEP"

#define AT_CMD_CWLAPOPT           "AT+CWLAPOPT"
#define AT_CMD_STATE              "AT+CWSTATE?"
#define AT_CMD_AP_STATE           "AT+CWJAP?"
#define AT_CMD_AP_SSID_PASSWD     "AT+USERSTACFG?"
#define AT_CMD_SCANF_AP           "AT+CWLAP"
#define AT_CMD_VERSION            "AT+GMR"
#define AT_CMD_START_SMARTCONFIG  "AT+CWSTARTSMART"
#define AT_CMD_WEBSERVER          "AT+WEBSERVER"
#define AT_CMD_STOP_SMARTCONFIG   "AT+CWSTOPSMART"
#define AT_CMD_RESTORE            "AT+RESTORE"

#define AT_CMD_UPDATE_BEGIN       "AT+LOCALUPDATEBEGIN"
#define AT_CMD_UPDATE_DATA        "AT+LOCALUPDATEDATA"
#define AT_CMD_UPDATE_END         "AT+LOCALUPDATEFINISH"

#define AT_DATAF_PREFIX           "\r\n+IPD"
#define AT_DATAF_CONNECT          ",CONNECT"
#define AT_DATAF_CLOSE            ",CLOSED"
#define AT_DATAF_DISCON           "WIFI DISCONNECT"
#define AT_DATAF_GOTIP            "WIFI GOT IP"

typedef enum {
    STA = 1,
    AP,
    ATA_AP,
} NetModeType;

typedef struct esp8266 {
    size_t packsize;
    size_t binarysize;
    char *filename;
} EspUpdateContextType;

AtAdaptorApi AtGetEsp8266Interface(void);
int32_t Esp8266WifiIsConnect(uint8_t *isConnect);
int32_t Esp8266OK();
int32_t Esp8266APGetWayIPSet(char *IP);
int32_t Esp8266DHCPIPSet(char *IP_Start, char *IP_End);
int32_t Esp8266Init(void);
int32_t Esp8266SetNetMode(NetModeType mode);
int32_t Esp8266JoinAP(char *pssid, char *ppasswd);
int32_t Esp8266QuitAP();
int32_t Esp8266SetupAutoConn(char mode);
int32_t Esp8266State(uint8_t *state);
int32_t Esp8266Rssi(int *rssi);
int32_t Esp8266Ssid(uint8_t *ssid);
int32_t Esp8266Version(char *version, uint32_t buffsize);
int8_t Esp8266GetLocalIp(int8_t *ip, int8_t *gw, int8_t *mask);
int8_t Esp8266GetLocalMac(int8_t *mac);
void Esp8266PreDeinit(void);
int32_t Esp8266Deinit(void);
int32_t Esp8266GetScanfAP(char *apList, int len);
int32_t Esp8266SetScanfCFG(void);
int32_t Esp8266GetScanfAP_SSID(char *ssid, char *apList, int apListLen);
int32_t Esp8266EnterAirKiss(void);
int32_t Esp8266QuitAirKiss(void);
int32_t Esp8266EnterSoftAP(char *SSID, char *password, char webMode);
int32_t Esp8266SsidPassword(uint8_t *ssid, uint8_t *password);
int32_t Esp8266CloseWEBSERVER(void);
int32_t Esp8266CleanStore();
int32_t Esp8266_AT_Update_Write(EspUpdateContextType *context);
int32_t Esp8266_AT_Update_Start(EspUpdateContextType *context);
void Esp8266SetRecvTimeout(int32_t id, int timeout);
void Esp8266SetSendTimeout(int32_t id, int timeout);
int32_t Esp8266ConnectLast();

#endif /* _ESP8266_H */
