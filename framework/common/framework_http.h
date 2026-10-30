/**
 * @file framework_http.h
 * @brief Framework HTTP Client Module Header
 * @details Provides non-blocking HTTP request capabilities with callback mechanism
 */

#ifndef _FRAMEWORK_HTTP_H_
#define _FRAMEWORK_HTTP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Constants and Definitions
 * ============================================================================ */

#define HTTP_MAX_URL_LEN        256
#define HTTP_MAX_HEADER_LEN     (2*1024)
#define HTTP_MAX_BODY_LEN       (10*1024)
#define HTTP_RESPONSE_BUF_SIZE  (40 * 1024)  /* 4KB response buffer */

/**
 * @brief HTTP request method enumeration
 */
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE
} http_method_e;


typedef enum {
    HTTP_DOWNLOAD_TYPE_FW_OTA = 0,
    HTTP_DOWNLOAD_TYPE_DFOTA = 1,
    HTTP_DOWNLOAD_TYPE_RES_OTA = 2,
    HTTP_DOWNLOAD_TYPE_WIFI_FW_OTA = 3,
    HTTP_DOWNLOAD_TYPE_FILE = 4
} http_download_type_t;

typedef enum {
    HTTP_DOWNLOAD_STATUS_START = 0,
    HTTP_DOWNLOAD_STATUS_PROGRESS = 1,
    HTTP_DOWNLOAD_STATUS_SUCCESS = 2,
    HTTP_DOWNLOAD_STATUS_FAIL = 3
} http_download_status_t;

typedef struct {
    http_download_type_t type;
    http_download_status_t status;
    int progress; // 0~100
    int err_code; // 0=ok, 其它为错误码
    char file_path[128];
} http_download_msg_t;

/**
 * @brief HTTP request result enumeration
 */
typedef enum {
    HTTP_RESULT_SUCCESS = 0,        /* Request completed successfully */
    HTTP_RESULT_ERROR_MEMORY,       /* Memory allocation failed */
    HTTP_RESULT_ERROR_NETWORK,      /* Network error */
    HTTP_RESULT_ERROR_TIMEOUT,      /* Request timeout */
    HTTP_RESULT_ERROR_INVALID_PARAM,/* Invalid parameter */
    HTTP_RESULT_ERROR_HTTP          /* HTTP protocol error */
} http_result_e;

/**
 * @brief HTTP request configuration structure
 */
typedef struct {
    http_method_e method;                   /* HTTP method */
    char url[HTTP_MAX_URL_LEN];            /* Request URL */
    char header[HTTP_MAX_HEADER_LEN];      /* Custom headers */
    char *body;                            /* Request body (for POST/PUT), allocated dynamically */
    uint32_t body_len;                     /* Body length in bytes */
    uint32_t timeout_ms;                    /* Request timeout in milliseconds */
    void *user_data;                        /* User data passed to callback */
    char *sni_host;        /* SNI host name for HTTPS */
} http_request_t;

/**
 * @brief HTTP response structure
 */
typedef struct {
    int status_code;                        /* HTTP status code (200, 404, etc.) */
    char *response_data;                    /* Response body data */
    uint32_t response_len;                  /* Response body length */
    http_result_e result;                   /* Request result */
} http_response_t;

/**
 * @brief HTTP request completion callback function type
 * @param response Response structure pointer
 * @param user_data User data from request
 */
typedef void (*http_callback_t)(http_response_t *response, void *user_data);
typedef void (*http_download_callback_t)(uint32_t msg_id, const char *save_path, void *user_data);
/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * @brief Initialize HTTP module
 * @details Creates HTTP task with 20KB stack
 * @return 0=success, negative=error
 */
int framework_http_init(void);

/**
 * @brief Deinitialize HTTP module
 */
void framework_http_deinit(void);

/**
 * @brief Queue HTTP download task executed in HTTP thread
 * @param url Download URL
 * @param save_path Local file path to store content
 * @param type Download type indicator
 * @return 0=queued successfully, negative=error
 */
int framework_http_download_file_with_md5(const char *url, const char *save_path, http_download_type_t type, const char *expected_md5);
int framework_http_download_file(const char *url, const char *save_path, http_download_type_t type);

/**
 * @brief Send asynchronous HTTP request
 * @param request Request configuration
 * @param callback Completion callback function
 * @return 0=request queued, negative=error
 * @note Callback will be invoked in HTTP task context when request completes
 */
int framework_http_request_async(http_request_t *request, http_callback_t callback);

/**
 * @brief Helper function to create POST request
 * @param url Request URL
 * @param body JSON body
 * @param header Custom headers (can be NULL)
 * @param callback Completion callback
 * @param user_data User data for callback
 * @return 0=success, negative=error
 */
int framework_http_get(const char *url, const char *header, http_callback_t callback, void *user_data);

int framework_http_post_json(const char *url, const char *body, const char *header,
                             http_callback_t callback, void *user_data);

int framework_http_post_json_with_sni(const char *url, const char *body, const char *header,
                             http_callback_t callback, void *user_data, char *sni_host);

int framework_http_download_file_callback_with_header(const char *url, const char *save_path, http_download_type_t type, const char *header, const char *expected_md5, http_download_callback_t callback, void *user_data);
int framework_http_download_file_callback_with_md5(const char *url, const char *save_path, http_download_type_t type, const char *expected_md5, http_download_callback_t callback, void *user_data);
int framework_http_download_file_callback(const char *url, const char *save_path, http_download_type_t type, http_download_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* _FRAMEWORK_HTTP_H_ */
