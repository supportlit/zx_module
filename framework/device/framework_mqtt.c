/**
 * @file framework_mqtt.c
 * @brief Framework MQTT client module implementation using Paho MQTT
 * @details Direct implementation based on Paho MQTT client library
 */

#include "framework_mqtt.h"
#include "framework_msg.h"
#include "framework_log.h"
#include "framework_led.h"
#include "framework_config.h"
#include "osi_api.h"
#include <stdlib.h>
#include <string.h>

/* Paho MQTT client library */
#include "mqttclient.h"

#define TAG "FRAMEWORK_MQTT"

// #define CIS_AWS_MQTT_ENABLED

#ifdef CIS_AWS_MQTT_ENABLED
#define CIS_MQTT_BROKER        "a2su98s08pbtzt-ats.iot.ap-south-1.amazonaws.com"
#define CIS_MQTT_SNI_HOST NULL
#define CIS_MQTT_BROKER_PORT        8883

static const char CIS_AWS_CA_CERT[] =
"-----BEGIN CERTIFICATE-----\n\
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n\
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n\
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n\
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n\
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n\
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n\
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n\
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n\
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n\
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n\
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n\
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n\
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n\
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n\
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n\
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n\
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n\
rqXRfboQnoZsG4q5WTP468SQvvG5\n\
-----END CERTIFICATE-----\n";

static const char CIS_AWS_CLIENT_CERT[] =
"-----BEGIN CERTIFICATE-----\n\
MIIDWTCCAkGgAwIBAgIUQtJGMMDlOjYiAjB5fmRZVdqYneUwDQYJKoZIhvcNAQEL\n\
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\n\
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MDUyOTEwMzcx\n\
N1oXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\n\
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANgjAXHBoX37ySAwF157\n\
jdKCBs21ksQJGHXqEHocQNT4g+sK5ZK8X5Unmmjw4O9P8VZUgu+ok9kLKibEBukW\n\
xTr3Uv3u1BXrn2EE6m18j9yv0q72qqKobRWx8Q1KI4tfqlAGaW0Fvr0ADYfVVfWr\n\
3viFDadjhTU4rCZLmJ0+7AmnrNhhiVjdBEjqrFWrQnd5J4MyI6ARcMYbU98qUoIU\n\
hyQfmOmewPqcr6a1qSWC9K5acgCoRa8NZn7e0jCgtFub9KLEQ75seGcRpS6SrH1a\n\
3mBysNuYdUnM5jVmw1Plt9FOD+Qjm9fJtSEdFPdPFpbeADhW0D/5lBtekpRz8dwf\n\
2p0CAwEAAaNgMF4wHwYDVR0jBBgwFoAU6VnpFbJ+k34wJJSdDQ9f+wqgB4IwHQYD\n\
VR0OBBYEFHfV+tTAF+KsK8GnELNxSuAvmpn9MAwGA1UdEwEB/wQCMAAwDgYDVR0P\n\
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQBB1b017mFR3VqbpE4xGeunlWzN\n\
6QZRD2xOMc0D0o6O5ViP0qLBA/YMnMyJnjjTiDGJAfYgHJ7Af8BS8TojAfwhm7U1\n\
9AlMX3n6t95pMCIKrwWXZzVscDe6wZwKcDoSQwbixySVDGvduXS7Smbx9gZSO3e8\n\
sNiFDrjoPcFzYaOJJTQ2ea6hPABsh6shVXHWA09ZFon0fQYOKyBcsOf0z6gnNm4i\n\
4QXljKrnaunk64yjoRDHNtxGsJpaCmJZpQqGx2P/ByJqkQHWDK6L61Ddcpk7x4MM\n\
ziXTUDw1/rhou4yoHX+EUUSBYqrcG4LYxkwRmQwdLDu9OuIYuCJaY3XewTUM\n\
-----END CERTIFICATE-----\n";

static const char CIS_AWS_CLIENT_KEY[] =
"-----BEGIN RSA PRIVATE KEY-----\n\
MIIEpQIBAAKCAQEA2CMBccGhffvJIDAXXnuN0oIGzbWSxAkYdeoQehxA1PiD6wrl\n\
krxflSeaaPDg70/xVlSC76iT2QsqJsQG6RbFOvdS/e7UFeufYQTqbXyP3K/Srvaq\n\
oqhtFbHxDUoji1+qUAZpbQW+vQANh9VV9ave+IUNp2OFNTisJkuYnT7sCaes2GGJ\n\
WN0ESOqsVatCd3kngzIjoBFwxhtT3ypSghSHJB+Y6Z7A+pyvprWpJYL0rlpyAKhF\n\
rw1mft7SMKC0W5v0osRDvmx4ZxGlLpKsfVreYHKw25h1SczmNWbDU+W30U4P5COb\n\
18m1IR0U908Wlt4AOFbQP/mUG16SlHPx3B/anQIDAQABAoIBAQDUj9DK1JiDM03B\n\
Byrkc/Stk80nICx/+UYCKTkdLo4gJWxvix7uaa5DHVjBXiyJjwCVW7YOBqeo7E7/\n\
Iwh80jUUYcpQDEKucDUlkFURalB9xLkZ82MrZyMpFAAyhrhgSKxR2YxzeRP2AohM\n\
D2Zjny1BEnG0Lq8CAYNkgjJMa2Ztspc8DndiGF5c79rhyr5D+KcgDGAHTgR9zsEz\n\
MnKe6ad5zTxN+JbOexxrbr48uAWu9PM10LXD68618vc5KAwJJmTZCjNnUqvTJ59J\n\
kmU7LTs8ECVBPSz2u7bLoyniICVXW7CoC5alJNWBwwWirjyHfVo1iyw70J5Sx6Z7\n\
GvP/Sf/NAoGBAPpKlSKNZyg4iiMT2XsGSY/JAy/B22zvW7V7LkeXqExlG8qMRJA0\n\
nzWgRgDH4dXMpxtCZq+HhXqka66OLIpnew6W5JtYd6ARN0qVrKw6zIwfQ/iFSCoS\n\
iRPf77awGjJyxNB0rbb4F1B0zPuRkDhHLUuieSHJwbdu7s0OzdpQJBS7AoGBAN0Q\n\
/72K4em42gt6EBb8/RsMBq9xDjCQIJy17QunLn4u2TQHOvXgw9Q/lDxwlxL6XQp+\n\
7cL0bSb5zyCdjzAV3zgx98Pw88m3LaN9EZXWxeLo2KLYAIF44sAmLxGbhX2kV4cU\n\
0jOwY8h24Yc7sx9pXnTvtFve1JF56xTrycFQ8gSHAoGBAMy41nU1ncvrOynn4Xpc\n\
MS03qLCWawkWWzf88mTCqnkaNnoc8zPUpujxGHEpDRKYW5LTZDRssAQZmq5jQhFP\n\
BPtw9roNyYMWsdgCxwL08TwnkqwjMFyhXpfwEh2muzbKYytyVGiVUlSPxN58OK+f\n\
oG/+DABUC2wjg5YcZsoLxQdjAoGBAJnGfV9IAKYoI+Tf39ld2mK6FtxrdEdruoPI\n\
OUBl5v13cTez39uxogc+wsrYNv7IkXf/mzyF8neuRdqWP0hfrorUl1kBf1PNzD1x\n\
fo4QfE6/UhLU1mU5xeGLrio9WLpxW0FnxmCWTpLfvznm2U78aOxG4dsWQh+AA3rm\n\
Gc4TlIZ5AoGAAsJIvHPczsKVGj9ejAyvGHNQ88Jgh2wrNbpVfPR+qZLa3gfIbeWS\n\
74WupmTi4l5Lu+Fp50hVSjY1t27MULH41XTEXVaeuBBoJdiaBo6ByiXkpgsbrEV4\n\
UeQtfGU3q1Te7+PhuWyspreVMDeSFvKqty54UO+eScoMpJRJM0AJDHU=\n\
-----END RSA PRIVATE KEY-----\n";
#endif

/* Default reconnection interval in milliseconds */
#define MQTT_RECONNECT_INTERVAL_MS  5000
#define MQTT_WORKER_STACK_SIZE      (6 * 1024)
#define MQTT_WORKER_PRIORITY        120
#define MQTT_WORKER_QUEUE_LEN       8

typedef enum {
    MQTT_WORKER_CMD_CONNECT = 0,
    MQTT_WORKER_CMD_DISCONNECT,
    MQTT_WORKER_CMD_RECONNECT,
    MQTT_WORKER_CMD_EXIT,
} mqtt_worker_cmd_t;

typedef struct {
    mqtt_worker_cmd_t cmd;
    uint32_t delay_ms;
} mqtt_worker_msg_t;

/**
 * @brief MQTT context structure
 * @details Holds all MQTT client state, configuration, and callbacks
 */
typedef struct {
    mqtt_config_t config;                   /**< MQTT configuration parameters */
    mqtt_state_e state;                     /**< Current connection state */
    
    /* Paho MQTT client handle */
    mqtt_client_t *client;                  /**< Paho client instance pointer */
    
    /* Callback functions registered by customer layer */
    mqtt_event_callback_t event_cb;         /**< Event callback (customer layer) */
    mqtt_message_callback_t message_cb;     /**< Message callback (customer layer) */
    mqtt_config_callback_t config_cb;       /**< Config callback (customer layer) */
    
    /* State management */
    uint32_t reconnect_count;               /**< Number of reconnection attempts */
    uint8_t initialized;                    /**< Initialization flag */
    
    osiThread_t *worker_thread;             /**< Dedicated MQTT worker thread */
    osiMessageQueue_t *worker_queue;        /**< Command queue for worker thread */
    uint8_t worker_running;                 /**< Worker thread running flag */
    mqtt_reconnect_handler_t reconnect_handler; /**< Reconnect callback handler */
    void *reconnect_data;                     /**< Reconnect callback data */
} mqtt_context_t;

/* Global MQTT context - holds all client state */
static mqtt_context_t g_mqtt_ctx = {0};

/* Forward declarations */
static void prv_mqtt_default_message_handler(const char *topic, const char *payload, uint32_t payload_len);
static void prv_paho_event_handler(void *client, int event_id);
static void prv_apply_custom_config(mqtt_config_t *config);
static void prv_mqtt_worker_thread(void *param);
static int prv_mqtt_enqueue_command(mqtt_worker_cmd_t cmd, uint32_t delay_ms);
static int prv_mqtt_do_connect(void);
static void prv_mqtt_do_disconnect(void);
static uint32_t prv_mqtt_calc_backoff_ms(uint32_t attempt_idx);

/**
 * @brief Paho MQTT message handler callback
 * @details Called when a message arrives on a subscribed topic.
 *          First tries customer callback; if unhandled, uses framework default handler.
 * @param client Paho MQTT client instance
 * @param msg Message data containing topic, payload, and metadata
 */
static void prv_paho_message_handler(void *client, message_data_t *msg)
{
    int ret = -1;
    
    if (msg == NULL || msg->message == NULL) {
        return;
    }

    OSI_LOGI(TAG, "MQTT RECV: topic=%s, len=%d", 
                  msg->topic_name, msg->message->payloadlen);

    /* Try customer message callback first */
    if (g_mqtt_ctx.message_cb) {
        ret = g_mqtt_ctx.message_cb(msg->topic_name, 
                                    (const char *)msg->message->payload,
                                    msg->message->payloadlen);
    }
    
    /* If customer layer did not handle (returns -1), use framework default handler */
    if (ret != 0) {
        prv_mqtt_default_message_handler(msg->topic_name,
                                         (const char *)msg->message->payload,
                                         msg->message->payloadlen);
    }
    
    /* Send framework internal message for data arrival event */
    framework_msg_t fmsg = {0};
    fmsg.id = MSG_MQTT_DATA_ARRIVED;
    fmsg.param1 = msg->message->payloadlen;
    framework_msg_send(&fmsg);
}

/**
 * @brief Framework default message handler
 * @details Fallback handler when customer layer does not process a message.
 *          Logs topic and payload for debugging purposes.
 * @param topic Topic name on which message was received
 * @param payload Message payload buffer
 * @param payload_len Length of payload in bytes
 */
static void prv_mqtt_default_message_handler(const char *topic, const char *payload, uint32_t payload_len)
{
    OSI_LOGI(TAG, "Framework default MQTT handler");
    OSI_LOGI(TAG, "  Topic: %s", topic);
    if (payload_len > 0 && payload_len < 256) {
        OSI_LOGI(TAG, "  Payload: %.*s", payload_len, payload);
    } else {
        OSI_LOGI(TAG, "  Payload length: %d", payload_len);
    }
}

static int prv_mqtt_enqueue_command(mqtt_worker_cmd_t cmd, uint32_t delay_ms)
{
    if (!g_mqtt_ctx.worker_queue) {
        OSI_LOGE(TAG, "MQTT worker queue not ready (cmd=%d)", cmd);
        return -1;
    }

    mqtt_worker_msg_t msg = {
        .cmd = cmd,
        .delay_ms = delay_ms,
    };

    if (!osiMessageQueuePut(g_mqtt_ctx.worker_queue, &msg)) {
        OSI_LOGE(TAG, "MQTT worker queue full (cmd=%d)", cmd);
        return -1;
    }

    return 0;
}

static void prv_mqtt_worker_thread(void *param)
{
    (void)param;
    mqtt_worker_msg_t msg;

    g_mqtt_ctx.worker_running = 1;
    OSI_LOGI(TAG, "MQTT worker thread started");

    while (1) {
        if (!osiMessageQueueGet(g_mqtt_ctx.worker_queue, &msg)) {
            osiThreadSleep(50);
            continue;
        }

        if (msg.delay_ms > 0) {
            osiThreadSleep(msg.delay_ms);
        }

        switch (msg.cmd) {
        case MQTT_WORKER_CMD_CONNECT:
            prv_mqtt_do_connect();
            break;
        case MQTT_WORKER_CMD_DISCONNECT:
            prv_mqtt_do_disconnect();
            break;
        case MQTT_WORKER_CMD_RECONNECT:
            OSI_LOGI(TAG, "MQTT worker RECONNECT command");
            prv_mqtt_do_disconnect();
            prv_mqtt_do_connect();
            break;
        case MQTT_WORKER_CMD_EXIT:
            prv_mqtt_do_disconnect();
            OSI_LOGI(TAG, "MQTT worker thread exiting");
            g_mqtt_ctx.worker_running = 0;
            osiThreadExit();
            return;
        default:
            break;
        }
    }
}

static void prv_mqtt_reconnect_handler(void *client, void* reconnect_date)
{
    OSI_LOGI(TAG, "MQTT reconnect handler called");
    if(g_mqtt_ctx.reconnect_handler) {
        g_mqtt_ctx.reconnect_handler(reconnect_date);
    } else {
        osiThreadSleep(MQTT_RECONNECT_INTERVAL_MS/5);
    }
}

/* Calculate exponential backoff delay: 1,2,4,8,16,32s (cap at 32s) */
static uint32_t prv_mqtt_calc_backoff_ms(uint32_t attempt_idx)
{
    uint32_t delay_sec = 1U << attempt_idx; /* attempt_idx starts at 0 */
    if (delay_sec > 32U) {
        delay_sec = 32U;
    }
    return delay_sec * 1000U;
}

static int prv_mqtt_do_connect(void)
{
    mqtt_client_t *client = NULL;
    static char port_str[8] = {0};
    int rc = 0;

    if (!g_mqtt_ctx.initialized) {
        OSI_LOGI(TAG, "MQTT not initialized");
        return -1;
    }

    if (g_mqtt_ctx.state == MQTT_STATE_CONNECTED) {
        OSI_LOGI(TAG, "MQTT already connected");
        return 0;
    }

    if (g_mqtt_ctx.state == MQTT_STATE_CONNECTING) {
        OSI_LOGI(TAG, "MQTT already connecting");
        return 0;
    }

    g_mqtt_ctx.state = MQTT_STATE_CONNECTING;

    /* Apply customer customization to config before connection */
    prv_apply_custom_config(&g_mqtt_ctx.config);

    /* Lease a Paho MQTT client instance from pool */
    client = mqtt_lease();
    if (client == NULL) {
        OSI_LOGI(TAG, "Failed to lease MQTT client");
        g_mqtt_ctx.state = MQTT_STATE_DISCONNECTED;
        return -1;
    }

    g_mqtt_ctx.client = client;

    /* Configure MQTT client parameters */
    #ifdef CIS_AWS_MQTT_ENABLED
    g_mqtt_ctx.config.enable_ssl = 1;
    static mqtt_cert_config_t s_cis_cert = {
        .ca_cert = CIS_AWS_CA_CERT,
        .client_cert = CIS_AWS_CLIENT_CERT,
        .client_key = CIS_AWS_CLIENT_KEY,
        .ca_cert_len = sizeof(CIS_AWS_CA_CERT) - 1,
        .client_cert_len = sizeof(CIS_AWS_CLIENT_CERT) - 1,
        .client_key_len = sizeof(CIS_AWS_CLIENT_KEY) - 1
    };
    g_mqtt_ctx.config.auth_type = MQTT_AUTH_CLIENT_CERT;
    g_mqtt_ctx.config.cert = &s_cis_cert; 
    mqtt_set_host(client, CIS_MQTT_BROKER);
    snprintf(port_str, sizeof(port_str), "%d", CIS_MQTT_BROKER_PORT);
    mqtt_set_sni_host(client, CIS_MQTT_SNI_HOST);
    mqtt_set_user_name(client, NULL);
    mqtt_set_password(client, NULL);
    #else
    mqtt_set_host(client, g_mqtt_ctx.config.host);
    mqtt_set_sni_host(client, (char *)g_mqtt_ctx.config.sni_host);
    snprintf(port_str, sizeof(port_str), "%d", g_mqtt_ctx.config.port);
    mqtt_set_user_name(client, g_mqtt_ctx.config.username);
    mqtt_set_password(client, g_mqtt_ctx.config.password);
    #endif
    mqtt_set_port(client, port_str);
    mqtt_set_client_id(client, g_mqtt_ctx.config.client_id);
    mqtt_set_clean_session(client, g_mqtt_ctx.config.clean_session);
    mqtt_set_keep_alive_interval(client, g_mqtt_ctx.config.keepalive_sec);
    mqtt_set_version(client, g_mqtt_ctx.config.mqtt_version);
    mqtt_set_alpn_list(client, (char **)g_mqtt_ctx.config.alpn_list);

    /* Configure SSL/TLS if enabled */
    if (g_mqtt_ctx.config.enable_ssl && g_mqtt_ctx.config.cert) {
        mqtt_set_ca(client, (char *)g_mqtt_ctx.config.cert->ca_cert);
        if (g_mqtt_ctx.config.auth_type == MQTT_AUTH_CLIENT_CERT) {
            mqtt_set_client_cert(client, (char *)g_mqtt_ctx.config.cert->client_cert);
            mqtt_set_client_pk(client, (char *)g_mqtt_ctx.config.cert->client_key);
        }
    }
    mqtt_set_ssl_enable(client, g_mqtt_ctx.config.enable_ssl);

    /* Configure Last Will and Testament (LWT) message if provided */
    if (g_mqtt_ctx.config.will) {
        mqtt_set_will_options(client, 
                             g_mqtt_ctx.config.will->topic,
                             g_mqtt_ctx.config.will->qos,
                             g_mqtt_ctx.config.will->retain,
                             g_mqtt_ctx.config.will->message);
    }

    /* Register event handler for connection state changes */
    mqtt_set_event_handler(client, prv_paho_event_handler);
    mqtt_set_reconnect_handler(client, (reconnect_handler_t)prv_mqtt_reconnect_handler);
    mqtt_set_reconnect_data(client, g_mqtt_ctx.reconnect_data);

    /* Initiate connection to MQTT broker (asynchronous) */
    OSI_LOGI(TAG, "Connecting to MQTT: %s:%s", client->mqtt_host, client->mqtt_port);
    OSI_LOGI(TAG, "  Client ID: %s", client->mqtt_client_id?client->mqtt_client_id:"<NULL>");
    OSI_LOGI(TAG, "  Username: %s", client->mqtt_user_name?client->mqtt_user_name:"<NULL>");
    // OSI_LOGI(TAG, "  Password: %s", client->mqtt_password);

    rc = mqtt_connect(client);
    if (rc < 0 ) {
        OSI_LOGI(TAG, "MQTT connect failed: %d", rc < 0 ? -rc: rc);
        mqtt_release(client);
        g_mqtt_ctx.client = NULL;
        g_mqtt_ctx.state = MQTT_STATE_DISCONNECTED;
        framework_msg_t msg = {0};
        msg.id = MSG_MQTT_CONNECT_FAILED;
        msg.param1 = osiGetTicks();
        framework_msg_send(&msg);

        /* Schedule exponential backoff reconnect: 1,2,4,8,16,32 seconds */
        g_mqtt_ctx.reconnect_count++;
        uint32_t delay_ms = prv_mqtt_calc_backoff_ms(g_mqtt_ctx.reconnect_count - 1U);
        OSI_LOGI(TAG, "MQTT schedule reconnect in %u ms (attempt %u)", (unsigned)delay_ms, (unsigned)g_mqtt_ctx.reconnect_count);
        if (prv_mqtt_enqueue_command(MQTT_WORKER_CMD_RECONNECT, delay_ms) != 0) {
            OSI_LOGE(TAG, "MQTT enqueue reconnect failed");
        }
        return -1;
    }
    OSI_LOGI(TAG, "MQTT connect initiated RC: %d", rc);
    return 0;
}

static void prv_mqtt_do_disconnect(void)
{
    if (!g_mqtt_ctx.initialized) {
        return;
    }

    if (g_mqtt_ctx.client) {
        mqtt_disconnect(g_mqtt_ctx.client, 1);
        mqtt_release(g_mqtt_ctx.client);
        g_mqtt_ctx.client = NULL;
    }

    g_mqtt_ctx.state = MQTT_STATE_DISCONNECTED;
    OSI_LOGI(TAG, "MQTT stopped");
}

/**
 * @brief Paho MQTT event handler callback
 * @details Handles MQTT connection state changes and triggers customer callbacks.
 *          Manages auto-reconnection logic and framework messaging.
 * @param client Paho MQTT client instance
 * @param event_id Event identifier (CLIENT_STATE_CONNECTED, CLIENT_STATE_DISCONNECTED, etc.)
 */
static void prv_paho_event_handler(void *client, int event_id)
{
    framework_msg_t msg = {0};
    OSI_LOGI(TAG, "Paho MQTT event handler: %d", event_id);
    switch (event_id) {
    case CLIENT_STATE_CONNECTED:
        OSI_LOGI(TAG, "MQTT connected");
        g_mqtt_ctx.state = MQTT_STATE_CONNECTED;
        g_mqtt_ctx.reconnect_count = 0;
        
        /* Trigger customer event callback for connection established */
        RTI_LOG("Calling customer event handler for CONNECTED: 0x%x event: %d", g_mqtt_ctx.event_cb, MQTT_EVENT_CONNECTED);
        if (g_mqtt_ctx.event_cb) {
            g_mqtt_ctx.event_cb(MQTT_EVENT_CONNECTED, NULL);
        }
        
        /* Send framework internal message for connected event */
        msg.id = MSG_MQTT_CONNECTED;
        msg.param1 = (uint32_t)client;
        framework_msg_send(&msg);
        break;
        
    case CLIENT_STATE_DISCONNECTED:
        OSI_LOGI(TAG, "MQTT disconnected");
        // framework_mqtt_stop();
        g_mqtt_ctx.state = MQTT_STATE_DISCONNECTED;
        
        /* Trigger customer event callback for disconnection */
        if (g_mqtt_ctx.event_cb) {
            g_mqtt_ctx.event_cb(MQTT_EVENT_DISCONNECTED, NULL);
        }
        
        /* Send framework internal message for disconnected event */
        msg.id = MSG_MQTT_DISCONNECTED;
        framework_msg_send(&msg);
        
        break;
        
    case CLIENT_STATE_CLEAN_SESSION:
        OSI_LOGI(TAG, "MQTT clean session");
        break;
        
    default:
        OSI_LOGI(TAG, "MQTT event: %d", event_id);
        break;
    }
}

/**
 * @brief Apply customer-layer configuration customization
 * @details Allows customer code to modify MQTT config before connection.
 *          Called during framework_mqtt_start() before connecting.
 * @param config Pointer to MQTT configuration structure
 */
static void prv_apply_custom_config(mqtt_config_t *config)
{
    if (g_mqtt_ctx.config_cb) {
        int ret = g_mqtt_ctx.config_cb(config);
        if (ret == 0) {
            OSI_LOGI(TAG, "Applied custom MQTT config");
        }
    }
}

/**
 * @brief Initialize MQTT client
 * @details Copies configuration to internal context and prepares MQTT subsystem.
 *          Does NOT initiate connection - call framework_mqtt_start() to connect.
 * @param config Pointer to MQTT configuration structure
 * @return 0 on success, -1 on error
 */
int framework_mqtt_init(mqtt_config_t *config)
{
    if (config == NULL) {
        OSI_LOGI(TAG, "Invalid MQTT config");
        return -1;
    }
    
    if (g_mqtt_ctx.initialized) {
        OSI_LOGI(TAG, "MQTT already initialized");
        return 0;
    }
    
    // memset(&g_mqtt_ctx, 0, sizeof(mqtt_context_t));
    
    /* Copy basic configuration parameters */
    if (config->host) {
        g_mqtt_ctx.config.host = strdup(config->host);
    }
    if (config->client_id) {
        g_mqtt_ctx.config.client_id = strdup(config->client_id);
    }
    
    /* Copy authentication configuration */
    g_mqtt_ctx.config.auth_type = config->auth_type;
    
    if (config->username) {
        g_mqtt_ctx.config.username = strdup(config->username);
    }
    if (config->password) {
        g_mqtt_ctx.config.password = strdup(config->password);
    }
    
    /* Copy certificate configuration for SSL/TLS */
    if (config->cert) {
        g_mqtt_ctx.config.cert = (mqtt_cert_config_t *)malloc(sizeof(mqtt_cert_config_t));
        if (g_mqtt_ctx.config.cert) {
            memcpy(g_mqtt_ctx.config.cert, config->cert, sizeof(mqtt_cert_config_t));
        }
    }
    
    /* Copy Last Will and Testament (LWT) configuration */
    if (config->will) {
        g_mqtt_ctx.config.will = (mqtt_will_config_t *)malloc(sizeof(mqtt_will_config_t));
        if (g_mqtt_ctx.config.will) {
            memset(g_mqtt_ctx.config.will, 0, sizeof(mqtt_will_config_t));
            if (config->will->topic) {
                g_mqtt_ctx.config.will->topic = strdup(config->will->topic);
            }
            if (config->will->message) {
                g_mqtt_ctx.config.will->message = strdup(config->will->message);
            }
            g_mqtt_ctx.config.will->qos = config->will->qos;
            g_mqtt_ctx.config.will->retain = config->will->retain;
        }
    }
    
    /* Copy connection parameters */
    g_mqtt_ctx.config.port = config->port;
    g_mqtt_ctx.config.keepalive_sec = (config->keepalive_sec > 0) ? config->keepalive_sec : 120;
    g_mqtt_ctx.config.enable_ssl = config->enable_ssl;
    g_mqtt_ctx.config.clean_session = config->clean_session;
    g_mqtt_ctx.config.mqtt_version = (config->mqtt_version > 0) ? config->mqtt_version : 4;
    g_mqtt_ctx.config.default_qos = config->default_qos;
    g_mqtt_ctx.config.alpn_list = config->alpn_list;
    
    g_mqtt_ctx.state = MQTT_STATE_DISCONNECTED;
    g_mqtt_ctx.initialized = 1;

    g_mqtt_ctx.worker_queue = osiMessageQueueCreate(MQTT_WORKER_QUEUE_LEN, sizeof(mqtt_worker_msg_t));
    if (!g_mqtt_ctx.worker_queue) {
        OSI_LOGE(TAG, "Failed to create MQTT worker queue");
        g_mqtt_ctx.initialized = 0;
        return -1;
    }

    g_mqtt_ctx.worker_thread = osiThreadCreate("mqtt_worker",
                                               prv_mqtt_worker_thread,
                                               NULL,
                                               MQTT_WORKER_PRIORITY,
                                               MQTT_WORKER_STACK_SIZE);
    if (!g_mqtt_ctx.worker_thread) {
        OSI_LOGE(TAG, "Failed to create MQTT worker thread");
        osiMessageQueueDelete(g_mqtt_ctx.worker_queue);
        g_mqtt_ctx.worker_queue = NULL;
        g_mqtt_ctx.initialized = 0;
        return -1;
    }
    
    OSI_LOGI(TAG, "MQTT initialized (auth=%d)", g_mqtt_ctx.config.auth_type);
    
    return 0;
}

/**
 * @brief Deinitialize MQTT client
 * @details Stops connection, releases all resources, and resets context.
 * @return 0 on success
 */
int framework_mqtt_deinit(void)
{
    if (!g_mqtt_ctx.initialized) {
        return 0;
    }
    
    /* Stop MQTT connection if active */
    framework_mqtt_stop();
    
    if (g_mqtt_ctx.worker_queue) {
        prv_mqtt_enqueue_command(MQTT_WORKER_CMD_EXIT, 0);
        /* Give worker a chance to exit gracefully */
        for (int i = 0; i < 50 && g_mqtt_ctx.worker_running; ++i) {
            osiThreadSleep(20);
        }
        osiMessageQueueDelete(g_mqtt_ctx.worker_queue);
        g_mqtt_ctx.worker_queue = NULL;
    }
    g_mqtt_ctx.worker_thread = NULL;
    g_mqtt_ctx.worker_running = 0;

    /* Free allocated resources */
    if (g_mqtt_ctx.config.host) free(g_mqtt_ctx.config.host);
    if (g_mqtt_ctx.config.client_id) free(g_mqtt_ctx.config.client_id);
    if (g_mqtt_ctx.config.username) free(g_mqtt_ctx.config.username);
    if (g_mqtt_ctx.config.password) free(g_mqtt_ctx.config.password);
    
    if (g_mqtt_ctx.config.cert) {
        free(g_mqtt_ctx.config.cert);
    }
    
    if (g_mqtt_ctx.config.will) {
        if (g_mqtt_ctx.config.will->topic) free(g_mqtt_ctx.config.will->topic);
        if (g_mqtt_ctx.config.will->message) free(g_mqtt_ctx.config.will->message);
        free(g_mqtt_ctx.config.will);
    }
    
    memset(&g_mqtt_ctx, 0, sizeof(mqtt_context_t));
    
    OSI_LOGI(TAG, "MQTT deinitialized");
    return 0;
}

/**
 * @brief Start MQTT connection
 * @details Leases Paho client, applies config, and initiates connection to broker.
 *          Connection result is reported via event callback asynchronously.
 *          On successful CONNACK, session_present flag indicates if broker
 *          retained a previous session (subscriptions, queued QoS1/2 messages).
 *          If session_present=1, client can skip re-subscribing.
 * @return 0 if connection initiated successfully, -1 on error
 */
int framework_mqtt_start(void)
{
    if (!g_mqtt_ctx.initialized) {
        OSI_LOGI(TAG, "MQTT not initialized");
        return -1;
    }

    return prv_mqtt_enqueue_command(MQTT_WORKER_CMD_CONNECT, 0);
}

/**
 * @brief Stop MQTT connection
 * @details Disconnects from broker and releases client resources.
 * @return 0 on success, -1 if not initialized
 */
int framework_mqtt_stop(void)
{
    if (!g_mqtt_ctx.initialized) {
        return -1;
    }

    return prv_mqtt_enqueue_command(MQTT_WORKER_CMD_DISCONNECT, 0);
}

/**
 * @brief Subscribe to MQTT topic
 * @details Subscribes to specified topic with given QoS level.
 *          Triggers MQTT_EVENT_SUBSCRIBED callback on success.
 * @param topic Topic filter string to subscribe
 * @param qos Quality of Service level (0, 1, or 2)
 * @return 0 on success, -1 on error
 */
int framework_mqtt_subscribe(const char *topic, uint8_t qos)
{
    int rc = 0;
    
    if (!g_mqtt_ctx.initialized || topic == NULL) {
        return -1;
    }
    
    if (g_mqtt_ctx.state != MQTT_STATE_CONNECTED || g_mqtt_ctx.client == NULL) {
        OSI_LOGI(TAG, "MQTT not connected");
        return -1;
    }
    
    /* Subscribe to topic with message handler callback */
    rc = mqtt_subscribe(g_mqtt_ctx.client, topic, (mqtt_qos_t)qos, prv_paho_message_handler);
    if (rc != 0) {
        OSI_LOGI(TAG, "MQTT subscribe failed: %d", rc);
        return -1;
    }
    
    /* Trigger subscription success event to customer callback */
    if (g_mqtt_ctx.event_cb) {
        g_mqtt_ctx.event_cb(MQTT_EVENT_SUBSCRIBED, (void *)topic);
    }
    
    OSI_LOGI(TAG, "MQTT subscribed: %s (QoS%d)", topic, qos);
    return 0;
}

/**
 * @brief Unsubscribe from MQTT topic
 * @details Cancels subscription to specified topic.
 *          Triggers MQTT_EVENT_UNSUBSCRIBED callback on success.
 * @param topic Topic filter string to unsubscribe
 * @return 0 on success, -1 on error
 */
int framework_mqtt_unsubscribe(const char *topic)
{
    int rc = 0;
    
    if (!g_mqtt_ctx.initialized || topic == NULL) {
        return -1;
    }
    
    if (g_mqtt_ctx.state != MQTT_STATE_CONNECTED || g_mqtt_ctx.client == NULL) {
        return -1;
    }
    
    rc = mqtt_unsubscribe(g_mqtt_ctx.client, topic);
    if (rc != 0) {
        OSI_LOGI(TAG, "MQTT unsubscribe failed: %d", rc);
        return -1;
    }
    
    /* Trigger unsubscription event to customer callback */
    if (g_mqtt_ctx.event_cb) {
        g_mqtt_ctx.event_cb(MQTT_EVENT_UNSUBSCRIBED, (void *)topic);
    }
    
    OSI_LOGI(TAG, "MQTT unsubscribed: %s", topic);
    return 0;
}

/**
 * @brief Publish MQTT message
 * @details Publishes payload to specified topic with given QoS and retain flag.
 *          Triggers MQTT_EVENT_PUBLISHED callback on success.
 * @param topic Topic string to publish to
 * @param payload Message payload buffer
 * @param payload_len Length of payload in bytes
 * @param qos Quality of Service level (0, 1, or 2)
 * @param retain Retain flag (1 to retain message on broker, 0 otherwise)
 * @return 0 on success, -1 on error
 */
int framework_mqtt_publish(const char *topic, const char *payload, 
                           uint32_t payload_len, uint8_t qos, uint8_t retain)
{
    int rc = 0;
    mqtt_message_t msg = {0};
    
    if (!g_mqtt_ctx.initialized || topic == NULL || payload == NULL) {
        return -1;
    }
    
    if (g_mqtt_ctx.state != MQTT_STATE_CONNECTED || g_mqtt_ctx.client == NULL) {
        OSI_LOGI(TAG, "MQTT not connected");
        return -1;
    }
    
    /* Construct MQTT message structure */
    msg.qos = (mqtt_qos_t)qos;
    msg.retained = retain;
    msg.payload = (void *)payload;
    msg.payloadlen = payload_len;
    
    /* Publish message to broker */
    rc = mqtt_publish(g_mqtt_ctx.client, topic, &msg);
    if (rc != 0) {
        OSI_LOGI(TAG, "MQTT publish failed: %d", rc);
        return -1;
    }
    
    /* Trigger publish success event to customer callback */
    if (g_mqtt_ctx.event_cb) {
        g_mqtt_ctx.event_cb(MQTT_EVENT_PUBLISHED, NULL);
    }
    
    OSI_LOGI(TAG, "MQTT published: %s (%d bytes)", topic, payload_len);
    return 0;
}

/**
 * @brief Register event callback
 * @details Customer layer provides callback for MQTT events (connected, disconnected, etc.)
 * @param callback Event callback function pointer
 * @return 0 on success
 */
int framework_mqtt_register_event_callback(mqtt_event_callback_t callback)
{
    g_mqtt_ctx.event_cb = callback;
    OSI_LOGI(TAG, "MQTT event callback registered: 0x%x", callback);
    return 0;
}

/**
 * @brief Register message callback
 * @details Customer layer provides callback to handle incoming messages on subscribed topics
 * @param callback Message callback function pointer
 * @return 0 on success
 */
int framework_mqtt_register_message_callback(mqtt_message_callback_t callback)
{
    g_mqtt_ctx.message_cb = callback;
    OSI_LOGI(TAG, "MQTT message callback registered");
    return 0;
}

/**
 * @brief Register config callback
 * @details Customer layer provides callback to customize MQTT config before connection
 * @param callback Config callback function pointer
 * @return 0 on success
 */
int framework_mqtt_register_config_callback(mqtt_config_callback_t callback)
{
    g_mqtt_ctx.config_cb = callback;
    OSI_LOGI(TAG, "MQTT config callback registered");
    return 0;
}

/**
 * @brief Register reconnect callback
 * @details Customer layer provides callback to handle reconnection events
 * @param callback Reconnect callback function pointer
 * @return 0 on success
 */
int framework_mqtt_register_reconnect_callback(mqtt_reconnect_handler_t callback, void *data)
{
    g_mqtt_ctx.reconnect_handler = callback;
    g_mqtt_ctx.reconnect_data = data;
    OSI_LOGI(TAG, "MQTT reconnect callback registered");
    return 0;
}

/**
 * @brief Get MQTT connection state
 * @return Current MQTT state (DISCONNECTED, CONNECTING, CONNECTED, etc.)
 */
mqtt_state_e framework_mqtt_get_state(void)
{
    return g_mqtt_ctx.state;
}

/**
 * @brief Check if MQTT is connected
 * @return 1 if connected, 0 otherwise
 */
uint8_t framework_mqtt_is_connected(void)
{
    return (g_mqtt_ctx.state == MQTT_STATE_CONNECTED) ? 1 : 0;
}

/**
 * @brief Get reconnection attempt count
 * @return Number of reconnection attempts since last successful connection
 */
uint32_t framework_mqtt_get_reconnect_count(void)
{
    return g_mqtt_ctx.reconnect_count;
}

/**
 * @brief Reset reconnection counter
 * @details Resets reconnection attempt count to zero
 */
void framework_mqtt_reset_reconnect_count(void)
{
    g_mqtt_ctx.reconnect_count = 0;
    OSI_LOGI(TAG, "MQTT reconnect count reset");
}
