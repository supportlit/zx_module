#ifndef CUSTOMER_REGISTER_H
#define CUSTOMER_REGISTER_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "framework_log.h"
#include "customer_config.h"

#ifndef TMS_REGISTRATION_TEST_ENABLE
#define TMS_REGISTRATION_TEST_ENABLE 0
#endif

// #define TAG "TMS_PARSE"
#define TMS_MAX_FIELDS 32

typedef struct {
    /* 1st  Index */ char response_code[16];       // "0000" = OK, "9999" = App Update Available
    /* 2nd  Index */ char mqtt_ip[64];             // "1.7.140.58"
    /* 3rd  Index */ int  mqtt_port;               // 9000
    /* 4th  Index */ char mqtt_user[64];           // "admin"
    /* 5th  Index */ char mqtt_pass[64];           // "Pine@987"
    /* 6th  Index */ int  mqtt_ssl_enable;         // 0 = Disable, 1 = Enable
    /* 7th  Index */ char mqtt_publish_key[64];    // Publish key (can be empty)
    /* 8th  Index */ char app_download_url[256];   // Soundbox App URL (when code == 9999)
    /* 9th  Index */ char participant_id[64];      // Participant / Partner ID
    /* 10th Index */ char application_code[64];    // Application Code
    /* 11th Index */ int  txn_ack_flag;            // Txn Acknowledgement Flag (0/1)
    /* 12th Index */ int  pending_txn_flag;        // Pending Txn Flag : Last 5 Txn (0/1)
    /* 13th Index */ int  welcome_file_flag;       // Welcome file flag (0/1)
    /* 14th Index */ char welcome_file_url[256];   // Welcome audio download URL
    /* 15th Index */ int  exception_file_flag;     // Exception file flag (0/1)
    /* 16th Index */ char exception_file_url[256]; // Exception file download URL
    /* 17th Index */ int  log_flag;                // Log Flag (0/1)
    /* 18th Index */ int  last_txn_flag;           // Last Transaction Flag (0/1)
    /* 19th Index */ int  lang_download_flag;      // Call API to download lang pack (0/1)
    /* 20th Index */ int  lang_numeric_code;       // Numeric lang code (0, 1, 2, ...)
    /* 21st Index */ char change_lang_req_id[32];  // Change lang request id or "0"
    /* 22nd Index */ char future_use[64];          // Reserved / For Future Use
} tms_registration_resp_t;
typedef struct {
    /* 1. Mandatory */ char serial_no[32];        // e.g., "00001504QR70W031184"
    /* 2. Mandatory */ char app_version[16];      // e.g., "1.0.0.3"
    /* 3. Optional  */ char audio_version[32];    // e.g., "MP3_V1.0_20221116"
    /* 4. Mandatory */ char tms_ip_port[128];     // e.g., "https://1.7.140.58/hmp"
    /* 5. Optional  */ char imei[24];             // e.g., "867772071407197"
    /* 6. Optional  */ char sim_no[32];           // e.g., "8991200050051682685F"
    /* 7. Optional  */ char firmware_version[16]; // e.g., "v1.2.0"
    /* 8. Mandatory */ char active_language[16];  // e.g., "1"
} tms_registration_req_t;
void tms_registration_task(void *param);
int tms_register_device_sync(tms_registration_resp_t *out_resp);
void customer_register_process_response(const tms_registration_resp_t *resp);
void customer_register_handle_lang_download(const tms_registration_resp_t *resp);
int customer_test_init(void);
int customer_test_get_flag(void);
void customer_register_clear_test_flag(void);
#endif
