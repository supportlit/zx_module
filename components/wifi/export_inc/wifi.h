/**
 @if Chinese
     * @brief       ��ģ���������� <strong>rtl 8710 WIFIģ��</strong> ��صĽӿ�
     * @author      jeffery
     * @date        2019-01-22
     * @version     V1.0
     *
     * @par �޶���¼
     * ����          | ����            | ����
     * ------------- | --------------- | ----
     * 2018/12/1     | jeffery         | �����ļ�
     * 2022/04/20    | Wenming         | ����Ӣ��ע��
 @else
     * @brief       This module is used to describe <strong>rtl 8710 WIFI module</strong> interfaces module
     * @author      jeffery
     * @date        2019-01-22
     * @version     V1.0
     *
     * @par Change History
     * Date          | Author          | Desc
     * ------------- | --------------- | ----
     * 2018/12/1     | jeffery         | Create file
     * 2022/04/20    | Wenming         | Add English description
 @endif
*/

/**
 * @defgroup WIFI WIFI
 * @{
*/

#ifndef __WIFI_API_H__
#define __WIFI_API_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
@if Chinese
    * @brief  ��ö�������г���ɨ�赽���ȵ㰲ȫ�����Լ������ȵ�ʱwifi֧�ֵİ�ȫ��ʽ.
  @else
    * @brief  This enumeration type lists the security types of scanned hotspots and the security modes supported by wifi when connecting to hotspots.
  @endif
*/
typedef enum {
    WIFI_AUTH_OPEN = 0, /**< authenticate mode : open */
    WIFI_AUTH_WEP, /**< authenticate mode : WEP */
    WIFI_AUTH_WPA_PSK, /**< authenticate mode : WPA_PSK */
    WIFI_AUTH_WPA2_PSK, /**< authenticate mode : WPA2_PSK */
    WIFI_AUTH_WPA_WPA2_PSK, /**< authenticate mode : WPA_WPA2_PSK */
    WIFI_AUTH_WPA2_ENTERPRISE, /**< authenticate mode : WPA2_ENTERPRISE */
    WIFI_AUTH_WPA3_PSK, /**< authenticate mode : WPA3_PSK */
    WIFI_AUTH_WPA2_WPA3_PSK, /**< authenticate mode : WPA2_WPA3_PSK */
    WIFI_AUTH_WAPI_PSK, /**< authenticate mode : WAPI_PSK */
    WIFI_AUTH_OWE, /**< authenticate mode : OWE */
    WIFI_AUTH_MAX,
    SECURITY_UNKNOWN = -1
} ap_security_t;

/**
 @if Chinese
    * @brief  ��ö���г���wifi������״̬.
 @else
    * @brief  This enumeration lists the connection status of wifi.
 @endif
  */
typedef enum {
    WIFI_JOIN_DISCONNECT = 0, /**< WIFI module disconnected                */
    WIFI_JOIN_CONNECTING, /**< WIFI module is connecting               */
    WIFI_JOIN_CONNECTED, /**< WIFI module connected                   */
} wifi_join_status;

#define WIFI_COMPILE_VERSION        0x61
#define WIFI_COMPILE_DATA           0x62
#define WIFI_COMPILE_AUTHOR         0x63
#define WIFI_SVN                    0x64

#define WIFI_OPT_APWEBDISABLE   0

/**
 @if Chinese
    * @brief  ��������SSID.
 @else
    * @brief  Used to describe an SSID.
 @endif
  */
typedef struct {
    unsigned char
    len; /**<@if Chinese SSID ���� @else SSID length                     @endif*/
    unsigned char
    val[33]; /**<@if Chinese SSID ���� (AP ����) @else   SSID name (AP name) @endif*/
} ssid_t;

/**
 @if Chinese
    * @brief  ��������6�ֽڳ��ȵ�mac��ַ
 @else
    * @brief  This parameter is used to describe a 6-byte MAC address
 @endif
*/
typedef struct {
    unsigned char
    octet[6]; /**<@if Chinese Ψһ�� 6-bytes MAC��ַ @else Only 6-byte MAC address    @endif*/
} mac_t;

/**
 @if Chinese
  * @brief ����һ���ȵ���Ϣ����Ϣ
  * @note  ����802.11Э��, ssid ����С�ڵ���32�ֽ�
 @else
  * @brief Information that describes a hotspot
  * @note  According to 802.11, the SSID length must be shorter than or equal to 32 bytes
 @endif
  */
typedef struct {
    int security_type; /**<@if Chinese ��ȫ����   @else  Safety type     @endif*/
    ssid_t SSID; /**<@if Chinese SSID       @else    SSID            @endif*/
    mac_t BSSID; /**<@if Chinese MAC��ַ    @else    MAC address     @endif*/
    signed short signal_strength; /**<@if Chinese �ź�ǿ��   @else    Signal strength @endif*/
} ap_info_t;

/**
 @if Chinese
  * @brief      ����wifi��ǰ������Ϣ
 @else
  * @brief      Description Current wifi network information
 @endif
  */
typedef struct {
    unsigned char ip[4]; /**<@if Chinese IP��ַ     @else    IP address      @endif*/
    unsigned char gate[4]; /**<@if Chinese ���ص�ַ   @else    Gate address    @endif*/
    unsigned char mask[4]; /**<@if Chinese �����ַ   @else   Mask address    @endif*/
    int security_type; /**<@if Chinese ��ȫ����   @else  Safety type     @endif*/
    int rssi; /**<@if Chinese �ź�ǿ��   @else    Signal strength @endif*/
    unsigned char ssid[33]; /**<@if Chinese ��������   @else    Network name    @endif*/
    mac_t mac; /**<@if Chinese MAC��ַ    @else    MAC address     @endif*/
} __attribute__((aligned(4))) network_info_t;

/**
 @if Chinese
    * @brief      AP  ����wifi��Ϣ
 @else
    * @brief     AP information
 @endif
  */
typedef struct {
    ssid_t ssid; /**ssid        */
    int security_type; /**< ��ȫ����  */
    unsigned char password[65]; /**< ����      */
    int password_len; /**< ���볤��  */
    int channel; /**< �ŵ�      */
    int language; /**< ��Ӣ��    */
} __attribute__((aligned(4))) ap_mode_info_t;

/**
 @if Chinese
    * @brief      ɨ��wifi�ȵ���ʾʱ����ʾ����
 @else
    * @brief     Display language when scanning wifi hotspot display
 @endif
  */
typedef enum {
    LANG_ENGLISH = 0,
    LANG_CHINESE = 1,
} wifi_language_e;

/**
 @if Chinese
      * @brief ��wifi
      * @return
      *         0       ----- �ɹ�.\n
      *         -1      ----- ʧ��.\n
 @else
      * @brief Open wifi
      * @return
      *         0       ----- Success.\n
      *         -1      ----- Fail.\n
 @endif
  */
int wifi_open(void);

/**
@if Chinese
  * @brief �ر�wifi
 @else
  * @brief Close wifi
 @endif
  */
void wifi_close(void);

/**
@if Chinese
  * @brief �ж�wifi�Ƿ��
  * @return
  *          1  wifi�Ѿ���.\n
  *          0  wifiû�д�.\n
 @else
  * @brief Check whether wifi is on
  * @return 1  wifi open.\n
  * @return 0  wifi not open.\n
@endif
  */
int wifi_is_open(void);

/**
@if Chinese
  * @brief  �����ȵ�ɨ��
  * @return - �ɹ�����0, ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
  * @note   ���øýӿں�ֻ����������ɨ��, �ӿڲ�������������������
@else
  * @brief  Start Hotspot Scanning
  * @return 0-Success.
    @return -1:Fail.(Use the errno value to get more error information)
  * @note   When this interface is called, it simply starts network scanning. The interface does not block and returns immediately
@endif
  */
int wifi_start_scan_networks(void);

/**
 @if Chinese
  * @brief  ��ȡɨ��״̬
  * @return
  *         0   ----- ����ɨ����.\n
  *         1   ----- ɨ�����.\n
  *        -1  ------ ɨ��ʧ��.\n
  * @note   ����wifi_start_scan_networks() ����ɨ���, ���Ե��øýӿڲ�ѯɨ��״̬��
  *         ��������µ��øýӿڷ���ֵ������
 @else
  * @brief  Get scan Status
  * @return 0   ----- Scaning.\n
  *         1   ----- Scan success.\n
  *        -1  ------ Scan fail.\n
  * @note   After a scan is started by calling wifi_start_scan_networks(), you can call this interface to query the scan status��
  *         In other cases, it is meaningless to call the return value of this interface
 @endif
  */
int wifi_is_scan_complete(void);

/**
 @if Chinese
  * @brief  ��ȡɨ����
  * @param[out]  ap_info    -   ���ڴ��ɨ����������
  * @param[out]  size       -   ���ɨ����������Ĵ�С
  * @return ��ɨ��ɹ��󣬷���ɨ�赽���ȵ�����, ɨ��ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
  * @note   �ýӿڻ���������ɨ����Ϻ�ŷ���                                         <br />
  *         int wifi_start_scan_networks(void),                                     <br />
  *         int wifi_is_scan_complete(void),                                         <br />
  *         int wifi_get_scan_data(ap_info_t *ap_info, int size)                     <br />
  *         �����ӿ��������������ʹ�÷�����                                         <br />                                                                             <br />
  * @par ʾ��:
    @code
            ��һ��:
                {
                    int return, is_scan_finish = 0, ap_num;
                    ap_info_t aplist[MAX_WIFI_INFO_LIST];
                    // ��ʼ�ȵ�ɨ��
                    return = wifi_start_scan_networks();
                    // �ȴ��ȵ�ɨ�����
                    do {
                        ... ;
                        ... ;
                        ... ;

                        is_scan_finish = wifi_is_scan_complete();
                    } while (is_scan_finish == 0);
                        ... ;
                        ... ;

                    // ��ȡ��ɨ�赽���ȵ���Ϣ
                    ap_num = wifi_get_scan_data(aplist, MAX_WIFI_INFO_LIST);
            }

            �ڶ���:
                {
                    int return, ap_num;
                    ap_info_t aplist[MAX_WIFI_INFO_LIST];
                    // ��ʼ�ȵ�ɨ��
                    return = wifi_start_scan_networks();
                //��δɨ�����ǰ, �ýӿڻ�һֱ����
                    ap_num = wifi_get_scan_data(aplist, MAX_WIFI_INFO_LIST);
            }
    @endcode
@else

  * @brief  Obtaining scan results
  * @param[out]  ap_info    -   An array to store the scan results
  * @param[out]  size       -   The size of the array that holds the scan results
  * @return If the scan succeeds, the number of detected hotspots is returned. If the scan fails, -1 is returned. Use the errno value to obtain more error information
  * @note   The interface will block until a scan is complete                                         <br />
  *         int wifi_start_scan_networks(void),                                     <br />
  *         int wifi_is_scan_complete(void),                                         <br />
  *         int wifi_get_scan_data(ap_info_t *ap_info, int size)                     <br />
  *         The three interfaces can be used in two ways together��                                         <br />                                                                             <br />
  * @par The sample:
    @code
            One:
                {
                    int return, is_scan_finish = 0, ap_num;
                    ap_info_t aplist[MAX_WIFI_INFO_LIST];
                    // Start Hotspot Scan
                    return = wifi_start_scan_networks();
                    //Wait for the hotspot scan to complete
                    do {
                        ... ;
                        ... ;
                        ... ;

                        is_scan_finish = wifi_is_scan_complete();
                    } while (is_scan_finish == 0);
                        ... ;
                        ... ;

                    // Obtain the detected hotspot information
                    ap_num = wifi_get_scan_data(aplist, MAX_WIFI_INFO_LIST);
            }

            Two:
                {
                    int return, ap_num;
                    ap_info_t aplist[MAX_WIFI_INFO_LIST];
                    // Start Hotspot Scan
                    return = wifi_start_scan_networks();
                //The interface is blocked until the scanning is complete
                    ap_num = wifi_get_scan_data(aplist, MAX_WIFI_INFO_LIST);
            }
    @endcode
@endif
  *
  */
int wifi_get_scan_data(ap_info_t *ap_info, int size);

/**
 @if Chinese
  * @brief  �����ȵ�
  * @return - �ɹ�����0, ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
  * @param[in]  ssid    -   ִ�����ӵ������ssid
  * @param[in]  key     -   ִ�����ӵ������key
  *
  * @note   �ýӿڵ��ú����������, ����ֵֻ�����ӿ�ִ���Ƿ�ɹ�, �������Ƿ����ӳɹ� <br />
  *         ���������Ƿ�ɹ�����
 @else
  * @brief  Connect wifi
  * @return -   0-success, -1:Fail, (Use the errno value to get more error information)
  * @param[in]  ssid    -   wifi ssid
  * @param[in]  key     -   wifi key
  *
  * @note   This interface is returned immediately after the call, and the return value only indicates whether the interface was successfully executed, not whether the connection was successful <br />
 @endif
  */
int wifi_connect(const char *ssid, const char *key);

/**
@if Chinese
  * @brief  �ȵ�����״̬
  * @return  int    -   @ref wifi_join_status
  *
  * @note   �ýӿ�����������ֵ, �μ�wifi_join_status. ִ�нӿ�          <br />
  *         int wifi_connect(const char *ssid, const char *key)         <br />
  *         ����Ե��øýӿ��ж��Ƿ����ӳɹ�                            <br />
  * @par ʾ��:
    @code
        {
            int connect_stat = WIFI_JOIN_DISCONNECT;
            ret = wifi_connect(ssidbuf, keybuf);
            // �ȴ�WIFI״̬
            if (!ret) {
                do {
                    connect_stat = wifi_get_join_status();
                    if (connect_stat == WIFI_JOIN_CONNECTING) {
                        ... ;
                        ... ;
                        msleep(50);
                    }
                } while (connect_stat == WIFI_JOIN_CONNECTING);
            }

            if(ret || connect_stat == WIFI_JOIN_DISCONNECT) {
                printf("����ʧ�� \n");
            }
        }
    @endcode

@else
  * @brief  wifi connect status
  * @return  int    -   @ref wifi_join_status
  *
  * @note   This interface has three return values, see wifi_join_status. Implement interface          <br />
  *         int wifi_connect(const char *ssid, const char *key)         <br />
  *         Call this interface to determine whether the connection is successful                            <br />
  * @par The sample:
    @code
        {
            int connect_stat = WIFI_JOIN_DISCONNECT;
            ret = wifi_connect(ssidbuf, keybuf);
            // wait wifi status
            if (!ret) {
                do {
                    connect_stat = wifi_get_join_status();
                    if (connect_stat == WIFI_JOIN_CONNECTING) {
                        ... ;
                        ... ;
                        msleep(50);
                    }
                } while (connect_stat == WIFI_JOIN_CONNECTING);
            }

            if(ret || connect_stat == WIFI_JOIN_DISCONNECT) {
                printf("Connect fail \n");
            }
        }
    @endcode
@endif
  */
int wifi_get_join_status(void);

/**
@if Chinese
  * @brief  wifi�Ƿ��������ȵ�
  * @return 1 ��ʾ�Ѿ������� 0 ��ʾδ����
@else
  * @brief  check wifi is connected
  * @return 0   -   Success
  * @return -1  -   Fail
 @endif
*/
int wifi_is_connected(void);

/**
 @if Chinese
  * @brief  �Ͽ�����
  * @return  - �ɹ�����0, ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
 @else
  * @brief  disconnect wifi
  * @return 0   -   Success
  * @return -1  -   Fail
@endif
  */
int wifi_disconnect(void);

/**
 @if Chinese
  * @brief  ��ȡ��ǰ������Ϣ
  * @param[out] info    -   ��ο��ṹ��network_info_t
  * @return - �ɹ�����0, ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
  @else
  * @brief  Get the current network information
  * @param[out] info    -   See the structure network_info_t
  * @return 0   -   Success
  * @return -1  -   Fail
 @endif
  */
int wifi_get_network_info(network_info_t *info);

/**
 @if Chinese
    * @brief  ��ȡwifi�̼��汾��Ϣ
 @else
    * @brief  Obtain wifi firmware version information
 @endif
  */
int wifi_get_firmware_info(int module, char *buf, int size);

/**
 @if Chinese
  * @brief  ����������AP�ȵ�
  * @param[in] timeout  -   ��ʱʱ�䣬��λΪ��
  * @param[in] language -   �ο� @ref  wifi_language_e
  * @return - �ɹ�����0, ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
 @else
  * @brief  Search for and connect to AP hotspots
  * @param[in] timeout  -   Timeout period, in seconds
  * @param[in] language -   reference @ref  wifi_language_e
  * @return
          0-Success<br>
          -1-Fail
  @endif
  */
int wifi_search_select_ap(int timeout, int language);

/**
 @if Chinese
  * @brief  ���wifi�Ƿ����
  * @return -   wifi���ŷ���1�����򷵻�0
 @else
  * @brief  Check wifi if alive
  * @return 1   -   alive
  * @return 0   -   not alive
 @endif
  */
int wifi_check(void);

/**
 @if Chinese
      * @brief  ����airkiss������WiFi��������ģʽ
      * @return - �ɹ�����0, ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
  @else
      * @brief  Start airKiss network and WiFi enters network mode
      * @return
          0-Success
          -1-Fail
  @endif
  */
int wifi_airkiss_start(void);

/**
 @if Chinese
  * @brief  ֹͣairkiss������WiFi�ָ���ͨģʽ
  * @return - �ɹ�����0, ʧ�ܷ���-1, ʹ��errnoֵ��ȡ���������Ϣ
 @else
  * @brief  Stop the Airkiss network and restore the normal WiFi mode
  * @return
      0-Success
      -1-Fail
 @endif
  */
int wifi_airkiss_stop(void);

/**
 @if Chinese
     * @brief  �ж�WiFi�Ƿ񻹴���airkiss����ģʽ
     * @return ��������ģʽ����1����ͨģʽ����0
 @else
     * @brief  Check whether WiFi is still in AirKiss network configuration mode
     * @return
     *1-Distribution network model<br>
     *0-Normal mode
 @endif
 */
int wifi_is_airkiss_stop(void);

/**
@if Chinese
 * @brief        �ж�airkiss �Ƿ����
 * @param[in]  ssid                 -   ִ�����ӵ������ssid
 * @param[in]  password             -   ִ�����ӵ������key
 * @retval          =1              -   �������
 * @retval          =0              -   ����δ���
 * @retval          -1              -   ��������ʧ��
 * @note            δʵ�ָýӿڣ����Ƿ���-1
@else
 * @brief            is airkiss is done?
 * @param[in]     ssid              -   wifi ssid
 * @param[in]     password          -   wifi key
 * @retval          =0              -   airKiss is no finish
 * @retval          -1              -   airKiss err
 * @retval          =1              -   Success
 * @note
@endif
*/
int wifi_airkiss_is_done(char ssid[33], char password[65]);

/**
@if Chinese
 * @brief        ��ʼAP ����.
 * @param[in]       ap_info         -   �����������Ϣ
 * @retval          =0              -   �ɹ�
 * @retval          -1              -   ʧ�ܣ���ͨ��errno��ȡ����Ĵ�����
 * @note
@else
 * @brief            start AP mode.
 * @param[in]       ap_info         -   ap mode infomation
 * @retval          =0              -   Success
 * @retval          -1              -   address
 * @note            If this interface is not implemented, -1 is always returned
@endif
*/
int wifi_ap_mode_start(ap_mode_info_t *ap_info);

/**
@if Chinese
 * @brief        �ж�ap ���� �Ƿ����
 * @param[in]  ssid                 -   ִ�����ӵ������ssid
 * @param[in]  password             -   ִ�����ӵ������key
 * @retval          =1              -   �������
 * @retval          =0              -   ����δ���
 * @note
@else
 * @brief            is ap mode is done?
 * @param[in]     ssid              -   wifi ssid
 * @param[in]     password          -   wifi key
 * @retval          =0              -   ap mode  is no finish
 * @retval          =1              -   Success
 * @note
@endif
*/
int wifi_ap_mode_is_done(char ssid[33], char password[65]);

/**
@if Chinese
* @brief        ֹͣAPģʽ.
* @retval          =0              -   �ɹ�
* @retval          -1              -   ʧ�ܣ���ͨ��errno��ȡ����Ĵ�����
* @note
@else
* @brief            stop ap mode.
* @retval          =0              -   Success
* @retval          -1              -   fail
* @note
@endif
*/
int wifi_ap_mode_stop(void);

/**
@if Chinese
* @brief        ������������SSID
* @param[in]  ssid         -   ִ������������ssid
* @retval          =0              -   �ɹ�
* @retval          -1              -   ʧ�ܣ���ͨ��errno��ȡ����Ĵ�����
* @note
@else
* @brief            Used to clean SSID information.
* @param[in]  ssid         -    input clean ssid
* @retval          =0              -   Success
* @retval          -1              -   address
* @note            If this interface is not implemented, -1 is always returned
@endif
*/

int wifi_clean_connet_info(const char *ssid);

/**
@if Chinese
 * @brief        ����ȡ�ź�ֵ
 * @param[out]      rssi                -   �ź�ֵ
 * @retval          =0              -   �ɹ�
 * @retval          -1              -   ʧ�ܣ���ͨ��errno��ȡ����Ĵ�����
 * @note            δʵ�ָýӿڣ����Ƿ���-1
@else
 * @brief            Used to get SSID signal strength.
 * @param[out]      rssi            -   signal strength
 * @retval          =0              -   Success
 * @retval          -1              -   address
 * @note            If this interface is not implemented, -1 is always returned
@endif
*/

int wifi_get_rssi(int *rssi);

/**
@if Chinese
 * @brief        ����WIFI����
 * @param[in]       cmd             -   �����
 * @param[in]       opt             -   �������������
 * @param[in]       optlen          -   ���Դ�С
 * @retval          =0              -   �ɹ�
 * @retval          -1              -   ʧ�ܣ���ͨ��errno��ȡ����Ĵ�����
 * @note            δʵ�ָýӿڣ����Ƿ���-1
@else
 * @brief           get host address info
 * @param[in]       cmd         -   command no
 * @param[in]       opt         -   input option
 * @param[in]       optlen      -   option len
 * @retval          =0              -   Success
 * @retval          -1              -   address
 * @note            If this interface is not implemented, -1 is always returned
@endif
*/
int wifi_set_opt(int cmd, void *opt, int optlen);

/**
@if Chinese
 * @brief        ���ڻ�ȡMAC
 * @param[out]      mac             -   �ź�ֵ
 * @retval          =0              -   �ɹ�
 * @retval          -1              -   ʧ�ܣ���ͨ��errno��ȡ����Ĵ�����
 * @note            δʵ�ָýӿڣ����Ƿ���-1
@else
 * @brief            Used to get SSID signal strength.
 * @param[out]      rssi            -   signal strength
 * @retval          =0              -   Success
 * @retval          -1              -   address
 * @note            If this interface is not implemented, -1 is always returned
@endif
*/
int wifi_get_mac(char *mac);

int wifi_update(char *binaryPath, int snyc);

int wifi_socket_is_enable();

int wifi_socket_get_enable(void);

bool Check_Wifi_Deinit_Done(void);

bool wifi_is_connecting(void);
#ifdef __cplusplus
}

#endif /* __cplusplus */

#endif /* __WIFI_API_H__ */
/** @} */
