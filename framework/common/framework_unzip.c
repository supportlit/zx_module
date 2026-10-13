#include "framework_unzip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_wrap.h"
#include "fs_api.h"
#include "framework_log.h"
#include "framework_msg.h"
#include "framework_audio.h"
#include "osi_api.h"
#include "zlib.h"
#include "contrib/minizip/unzip.h"

#if !defined(UINT32_MAX)
typedef unsigned int uint32_t;
#endif
#if !defined(UINT8_MAX)
typedef unsigned char uint8_t;
#endif

#define TAG "FRAMEWORK_UNZIP"
#define UNZIP_TASK_STACK_SIZE   (12 * 1024)
#define UNZIP_TASK_PRIORITY     120
#define UNZIP_QUEUE_SIZE        4
#define UNZIP_PATH_MAX          256
#define APP_UNZIP_MAX_PATH      512
#define APP_UNZIP_BUFFER_SIZE   2048
#define APP_UNZIP_CZIP_MAGIC    0x12345678u

typedef uint8_t framework_unzip_bool_t;
#define FW_UNZIP_FALSE 0
#define FW_UNZIP_TRUE  1

typedef void (*framework_unzip_progress_cb)(const char *entry_name,
                                            uint32_t current_index,
                                            uint32_t total_entries,
                                            void *user_ctx);

typedef struct {
    char zip_path[UNZIP_PATH_MAX];
    char output_dir[UNZIP_PATH_MAX];
    framework_unzip_type_t type;
} framework_unzip_job_t;

typedef struct {
    osiThread_t *task_handle;
    osiMessageQueue_t *queue;
    framework_unzip_bool_t initialized;
} framework_unzip_context_t;

typedef struct {
    framework_unzip_type_t type;
    char base_dir[UNZIP_PATH_MAX];
    int last_percent;
} framework_unzip_progress_ctx_t;

static framework_unzip_context_t g_unzip_ctx = {0};
static char g_unzip_msg_path[UNZIP_PATH_MAX];

static void prv_unzip_task(void *param);
static void prv_execute_unzip_job(framework_unzip_job_t *job);
static void prv_unzip_progress_adapter(const char *entry_name,
                                       uint32_t current,
                                       uint32_t total,
                                       void *user_ctx);
static void prv_send_unzip_msg(uint32_t msg_id,
                               framework_unzip_type_t type,
                               int progress,
                               int err,
                               const char *path);
static int prv_ascii_tolower(int ch);
static framework_unzip_bool_t prv_has_suffix_nocase(const char *str, const char *suffix);
static framework_unzip_bool_t prv_is_czip_file(const char *path);
static int prv_unzip_folder(const char *zip_file_path,
                            const char *output_dir,
                            framework_unzip_progress_cb progress_cb,
                            void *user_ctx);
static void prv_normalize_path(char *path);
static void prv_trim_trailing_slash(char *path);
static void prv_try_mkdir(const char *path);
static void prv_create_dirs_recursive(char *path_buf);
static void prv_create_directory_recursive(const char *dir_path);
static void prv_create_parent_directories(const char *file_path);
static framework_unzip_bool_t prv_is_directory_entry(const char *filename);
static int prv_extract_current_file(unzFile zip_file, const char *dest_path);
static int prv_unzip_czip(const char *zip_file_path,
                          const char *output_dir,
                          framework_unzip_progress_cb progress_cb,
                          void *user_ctx);
static int prv_czip_read_name(int fd, char *name, size_t buf_len, uint32_t *bytes_consumed);
static void prv_czip_emit_progress(framework_unzip_progress_cb progress_cb,
                                   const char *entry_name,
                                   uint32_t current,
                                   uint32_t total,
                                   void *user_ctx);
static uint32_t prv_crc32_init(void);
static uint32_t prv_crc32_update(uint32_t crc, const void *p, size_t len);

static const uint32_t g_framework_unzip_crc32_table[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau,
    0x076dc419u, 0x706af48fu, 0xe963a535u, 0x9e6495a3u,
    0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u,
    0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u,
    0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu,
    0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
    0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu,
    0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u,
    0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u,
    0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu,
    0x35b5a8fau, 0x42b2986cu, 0xdbbbc9d6u, 0xacbcf940u,
    0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
    0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u,
    0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
    0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u,
    0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du,
    0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au,
    0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
    0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u,
    0x7f6a0dbbu, 0x086d3d2du, 0x91646c97u, 0xe6635c01u,
    0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
    0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u,
    0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau, 0xfcb9887cu,
    0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
    0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u,
    0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu,
    0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u,
    0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
    0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u,
    0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
    0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u,
    0x59b33d17u, 0x2eb40d81u, 0xb7bd5c3bu, 0xc0ba6cadu,
    0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au,
    0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u,
    0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u,
    0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
    0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu,
    0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u,
    0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu,
    0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u,
    0xd6d6a3e8u, 0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u,
    0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u,
    0xdf60efc3u, 0xa867df55u, 0x316e8eefu, 0x4669be79u,
    0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u,
    0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu,
    0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u,
    0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
    0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au,
    0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u,
    0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u,
    0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u,
    0x86d3d2d4u, 0xf1d4e242u, 0x68ddb3f8u, 0x1fda836eu,
    0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
    0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu,
    0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
    0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u,
    0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu,
    0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u,
    0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
    0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u,
    0xbad03605u, 0xcdd70693u, 0x54de5729u, 0x23d967bfu,
    0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
    0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du
};

int framework_unzip_init(void)
{
    if (g_unzip_ctx.initialized) {
        OSI_LOGW(TAG, "unzip module already initialized");
        return 0;
    }

    g_unzip_ctx.queue = osiMessageQueueCreate(UNZIP_QUEUE_SIZE, sizeof(framework_unzip_job_t *));
    if (!g_unzip_ctx.queue) {
        OSI_LOGE(TAG, "failed to create unzip queue");
        return -1;
    }

    g_unzip_ctx.task_handle = osiThreadCreate("unzip_task",
                                              prv_unzip_task,
                                              NULL,
                                              UNZIP_TASK_PRIORITY,
                                              UNZIP_TASK_STACK_SIZE);
    if (!g_unzip_ctx.task_handle) {
        OSI_LOGE(TAG, "failed to create unzip task");
        osiMessageQueueDelete(g_unzip_ctx.queue);
        g_unzip_ctx.queue = NULL;
        return -1;
    }

    g_unzip_ctx.initialized = FW_UNZIP_TRUE;
    OSI_LOGI(TAG, "unzip module initialized");
    return 0;
}

void framework_unzip_deinit(void)
{
    if (!g_unzip_ctx.initialized) {
        return;
    }

    if (g_unzip_ctx.task_handle) {
        osiThreadExit();
        g_unzip_ctx.task_handle = NULL;
    }
    if (g_unzip_ctx.queue) {
        osiMessageQueueDelete(g_unzip_ctx.queue);
        g_unzip_ctx.queue = NULL;
    }
    g_unzip_ctx.initialized = FW_UNZIP_FALSE;
    OSI_LOGI(TAG, "unzip module deinitialized");
}

int framework_unzip_start(const char *zip_path,
                          const char *output_dir,
                          framework_unzip_type_t type)
{
    if (!g_unzip_ctx.initialized) {
        OSI_LOGE(TAG, "unzip module not initialized");
        return -1;
    }
    if (!zip_path || !output_dir || zip_path[0] == '\0' || output_dir[0] == '\0') {
        OSI_LOGE(TAG, "invalid unzip parameters");
        return -2;
    }

    framework_unzip_job_t *job = malloc(sizeof(framework_unzip_job_t));
    if (!job) {
        OSI_LOGE(TAG, "failed to allocate unzip job");
        return -3;
    }
    memset(job, 0, sizeof(*job));
    strncpy(job->zip_path, zip_path, sizeof(job->zip_path) - 1);
    strncpy(job->output_dir, output_dir, sizeof(job->output_dir) - 1);
    job->type = type;

    if (!osiMessageQueuePut(g_unzip_ctx.queue, &job)) {
        OSI_LOGE(TAG, "failed to enqueue unzip job");
        free(job);
        return -4;
    }

    OSI_LOGI(TAG, "queued unzip job %s -> %s", job->zip_path, job->output_dir);
    return 0;
}

static void prv_unzip_task(void *param)
{
    (void)param;
    OSI_LOGI(TAG, "unzip task started");

    while (1) {
        framework_unzip_job_t *job = NULL;
        if (osiMessageQueueGet(g_unzip_ctx.queue, &job)) {
            if (job) {
                prv_execute_unzip_job(job);
                free(job);
            }
        }
    }
}

static void prv_execute_unzip_job(framework_unzip_job_t *job)
{
    if (!job) {
        return;
    }

    framework_unzip_progress_ctx_t ctx = {
        .type = job->type,
        .last_percent = 0,
    };
    strncpy(ctx.base_dir, job->output_dir, sizeof(ctx.base_dir) - 1);
    ctx.base_dir[sizeof(ctx.base_dir) - 1] = '\0';

    prv_send_unzip_msg(MSG_UNZIP_START, job->type, 0, 0, job->output_dir);

    int ret = prv_unzip_folder(job->zip_path, job->output_dir, prv_unzip_progress_adapter, &ctx);
    if (ret == 0) {
        prv_send_unzip_msg(MSG_UNZIP_SUCCESS, job->type, 100, 0, job->output_dir);
    } else {
        prv_send_unzip_msg(MSG_UNZIP_FAIL, job->type, ctx.last_percent, ret, job->output_dir);
    }
}

static void prv_unzip_progress_adapter(const char *entry_name,
                                       uint32_t current,
                                       uint32_t total,
                                       void *user_ctx)
{
    framework_unzip_progress_ctx_t *ctx = (framework_unzip_progress_ctx_t *)user_ctx;
    if (!ctx || total == 0) {
        return;
    }

    int percent = (int)((current * 100U) / total);
    if (percent > 100) {
        percent = 100;
    }
    ctx->last_percent = percent;

    char combined_path[UNZIP_PATH_MAX * 2];
    if (entry_name && entry_name[0] != '\0') {
        snprintf(combined_path, sizeof(combined_path), "%s/%s", ctx->base_dir, entry_name);
    } else {
        strncpy(combined_path, ctx->base_dir, sizeof(combined_path) - 1);
        combined_path[sizeof(combined_path) - 1] = '\0';
    }

    prv_send_unzip_msg(MSG_UNZIP_PROGRESS, ctx->type, percent, 0, combined_path);
}

static void prv_send_unzip_msg(uint32_t msg_id,
                               framework_unzip_type_t type,
                               int progress,
                               int err,
                               const char *path)
{
    framework_msg_t msg = {0};
    msg.id = msg_id;
    msg.param1 = type;
    msg.param2 = progress;
    if (path) {
        strncpy(g_unzip_msg_path, path, sizeof(g_unzip_msg_path) - 1);
        g_unzip_msg_path[sizeof(g_unzip_msg_path) - 1] = '\0';
        msg.param3 = (uint32_t)g_unzip_msg_path;
    }

    OSI_LOGI(TAG, "send msg 0x%x type=%d progress=%d err=%d path=%s",
             msg_id, type, progress, err, path ? path : "");
    framework_msg_send(&msg);
}

static int prv_ascii_tolower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch + ('a' - 'A');
    }
    return ch;
}

static framework_unzip_bool_t prv_has_suffix_nocase(const char *str, const char *suffix)
{
    if (!str || !suffix) {
        return FW_UNZIP_FALSE;
    }
    size_t len_str = strlen(str);
    size_t len_suf = strlen(suffix);
    if (len_suf == 0 || len_suf > len_str) {
        return FW_UNZIP_FALSE;
    }
    const char *str_tail = str + (len_str - len_suf);
    for (size_t i = 0; i < len_suf; ++i) {
        if (prv_ascii_tolower((int)str_tail[i]) != prv_ascii_tolower((int)suffix[i])) {
            return FW_UNZIP_FALSE;
        }
    }
    return FW_UNZIP_TRUE;
}

static framework_unzip_bool_t prv_is_czip_file(const char *path)
{
    return prv_has_suffix_nocase(path, ".czip");
}

static uint32_t prv_crc32_init(void)
{
    return 0;
}

static uint32_t prv_crc32_update(uint32_t crc, const void *p, size_t len)
{
    if (p == NULL || len == 0) {
        return crc;
    }
    const uint8_t *pc = (const uint8_t *)p;
    crc ^= 0xffffffffu;
    while (len-- > 0) {
        crc = g_framework_unzip_crc32_table[(crc ^ *pc++) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

static void prv_normalize_path(char *path)
{
    if (!path) {
        return;
    }
    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (path[i] == '\\') {
            path[i] = '/';
        }
    }
}

static void prv_trim_trailing_slash(char *path)
{
    if (!path) {
        return;
    }
    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
        path[len - 1] = '\0';
        --len;
    }
}

static void prv_try_mkdir(const char *path)
{
    if (!path || path[0] == '\0') {
        return;
    }

    size_t len = strlen(path);
    if (len == 2 && path[1] == ':') {
        return; // Skip drive root like "D:"
    }
    fs_makedir(path);
}

static void prv_create_dirs_recursive(char *path_buf)
{
    prv_normalize_path(path_buf);
    prv_trim_trailing_slash(path_buf);

    size_t len = strlen(path_buf);
    if (len == 0) {
        return;
    }

    size_t start = 0;
    if (len > 2 && path_buf[1] == ':') {
        start = 2;
    }

    for (size_t i = start; i < len; ++i) {
        if (path_buf[i] == '/') {
            char saved = path_buf[i];
            path_buf[i] = '\0';
            prv_try_mkdir(path_buf);
            path_buf[i] = saved;
        }
    }
    prv_try_mkdir(path_buf);
}

static void prv_create_directory_recursive(const char *dir_path)
{
    if (!dir_path) {
        return;
    }

    char temp[APP_UNZIP_MAX_PATH * 2];
    strncpy(temp, dir_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    prv_create_dirs_recursive(temp);
}

static void prv_create_parent_directories(const char *file_path)
{
    if (!file_path) {
        return;
    }

    char temp[APP_UNZIP_MAX_PATH * 2];
    strncpy(temp, file_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    prv_normalize_path(temp);

    char *last_sep = strrchr(temp, '/');
    if (!last_sep) {
        last_sep = strrchr(temp, '\\');
    }
    if (!last_sep) {
        return;
    }

    *last_sep = '\0';
    if (temp[0] != '\0') {
        prv_create_directory_recursive(temp);
    }
}

static framework_unzip_bool_t prv_is_directory_entry(const char *filename)
{
    if (!filename) {
        return FW_UNZIP_FALSE;
    }
    size_t len = strlen(filename);
    return (len > 0 && (filename[len - 1] == '/' || filename[len - 1] == '\\')) ? FW_UNZIP_TRUE : FW_UNZIP_FALSE;
}

static int prv_extract_current_file(unzFile zip_file, const char *dest_path)
{
    if (unzOpenCurrentFile(zip_file) != UNZ_OK) {
        OSI_LOGE(TAG, "Failed to open entry for extraction");
        return -5;
    }

    prv_create_parent_directories(dest_path);

    int fd = fs_open(dest_path, "wb");
    if (fd <= 0) {
        OSI_LOGE(TAG, "Cannot create output file %s", dest_path);
        unzCloseCurrentFile(zip_file);
        return -6;
    }

    char buffer[APP_UNZIP_BUFFER_SIZE];
    int bytes_read = 0;
    while ((bytes_read = unzReadCurrentFile(zip_file, buffer, sizeof(buffer))) > 0) {
        while (audio_resource_is_playing())
        {
            osiThreadSleep(20);
        }
        size_t written = fs_write(buffer, 1, bytes_read, fd);
        if (written != (size_t)bytes_read) {
            OSI_LOGE(TAG, "Failed to write file %s", dest_path);
            fs_close(fd);
            unzCloseCurrentFile(zip_file);
            return -7;
        }
    }

    fs_close(fd);
    if (bytes_read < 0) {
        OSI_LOGE(TAG, "Error while reading zip entry");
        unzCloseCurrentFile(zip_file);
        return -8;
    }

    unzCloseCurrentFile(zip_file);
    return 0;
}

static int prv_unzip_folder(const char *zip_file_path,
                            const char *output_dir,
                            framework_unzip_progress_cb progress_cb,
                            void *user_ctx)
{
    if (!zip_file_path || !output_dir) {
        return -1;
    }

    if (prv_is_czip_file(zip_file_path)) {
        return prv_unzip_czip(zip_file_path, output_dir, progress_cb, user_ctx);
    }

    unzFile zip_file = unzOpen(zip_file_path);
    if (zip_file == NULL) {
        OSI_LOGE(TAG, "Cannot open zip file %s", zip_file_path);
        return -2;
    }

    unz_global_info global_info;
    if (unzGetGlobalInfo(zip_file, &global_info) != UNZ_OK) {
        OSI_LOGE(TAG, "Failed to get zip global info");
        unzClose(zip_file);
        return -3;
    }

    uint32_t total_entries = (uint32_t)global_info.number_entry;
    if (total_entries == 0) {
        total_entries = 1;
    }

    int ret = unzGoToFirstFile(zip_file);
    if (ret != UNZ_OK) {
        OSI_LOGE(TAG, "Cannot go to first file");
        unzClose(zip_file);
        return -4;
    }

    uint32_t current_entry = 0;
    int go_next_ret = UNZ_OK;

    do {
        char filename[APP_UNZIP_MAX_PATH] = {0};
        unz_file_info file_info = {0};

        if (unzGetCurrentFileInfo(zip_file, &file_info, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK) {
            OSI_LOGE(TAG, "Cannot read file info");
            ret = -5;
            break;
        }

        framework_unzip_bool_t is_dir = prv_is_directory_entry(filename);
        char full_path[APP_UNZIP_MAX_PATH * 2];
        snprintf(full_path, sizeof(full_path), "%s/%s", output_dir, filename);
        prv_normalize_path(full_path);

        if (is_dir) {
            prv_create_directory_recursive(full_path);
            OSI_LOGI(TAG, "Created directory %s", full_path);
        } else {
            ret = prv_extract_current_file(zip_file, full_path);
            if (ret != 0) {
                break;
            }
            OSI_LOGI(TAG, "Extracted file %s", full_path);
        }

        current_entry++;
        if (progress_cb) {
            progress_cb(filename, current_entry, total_entries, user_ctx);
        }

        go_next_ret = unzGoToNextFile(zip_file);
    } while (go_next_ret == UNZ_OK);

    if (ret == 0 && go_next_ret != UNZ_END_OF_LIST_OF_FILE) {
        OSI_LOGE(TAG, "Unexpected error while iterating zip entries");
        ret = -6;
    }

    unzClose(zip_file);
    return ret;
}

static int prv_czip_read_name(int fd, char *name, size_t buf_len, uint32_t *bytes_consumed)
{
    if (!name || buf_len == 0) {
        return -1;
    }
    size_t written = 0;
    uint32_t consumed = 0;
    while (1) {
        char c = 0;
        if (fs_read(&c, 1, 1, fd) != 1) {
            return -2;
        }
        consumed++;
        if (written + 1 < buf_len) {
            name[written++] = c;
        }
        if (c == '\0') {
            break;
        }
    }
    if (written == 0) {
        name[0] = '\0';
    } else {
        name[buf_len - 1] = '\0';
    }
    if (bytes_consumed) {
        *bytes_consumed = consumed;
    }
    return (int)written;
}

static void prv_czip_emit_progress(framework_unzip_progress_cb progress_cb,
                                   const char *entry_name,
                                   uint32_t current,
                                   uint32_t total,
                                   void *user_ctx)
{
    if (!progress_cb || total == 0) {
        return;
    }
    if (current > total) {
        current = total;
    }
    progress_cb(entry_name, current, total, user_ctx);
}

static int prv_unzip_czip(const char *zip_file_path,
                          const char *output_dir,
                          framework_unzip_progress_cb progress_cb,
                          void *user_ctx)
{
    int fd = fs_open(zip_file_path, "rb");
    if (fd <= 0) {
        OSI_LOGE(TAG, "Cannot open czip file %s", zip_file_path);
        return -10;
    }

    uint32_t total_size = fs_size(zip_file_path);
    if (total_size == 0) {
        fs_close(fd);
        OSI_LOGE(TAG, "Invalid czip file size");
        return -11;
    }

    uint32_t magic = 0;
    uint32_t checksum = 0;
    if (fs_read(&magic, 1, 4, fd) != 4 || fs_read(&checksum, 1, 4, fd) != 4) {
        fs_close(fd);
        OSI_LOGE(TAG, "Failed to read czip header");
        return -12;
    }
    if (magic != APP_UNZIP_CZIP_MAGIC) {
        fs_close(fd);
        OSI_LOGE(TAG, "Invalid czip magic: 0x%x", magic);
        return -13;
    }

    char buffer[APP_UNZIP_BUFFER_SIZE];
    uint32_t crc = prv_crc32_init();
    int chunk = 0;
    while ((chunk = (int)fs_read(buffer, 1, sizeof(buffer), fd)) > 0) {
        crc = prv_crc32_update(crc, buffer, (size_t)chunk);
    }
    if (crc != checksum) {
        fs_close(fd);
        OSI_LOGE(TAG, "czip checksum mismatch (calc=0x%x, expected=0x%x)", crc, checksum);
        return -14;
    }

    fs_seek(fd, 8, SEEK_SET);
    uint32_t processed = 8;

    char base_dir_buf[APP_UNZIP_MAX_PATH * 2];
    strncpy(base_dir_buf, output_dir, sizeof(base_dir_buf) - 1);
    base_dir_buf[sizeof(base_dir_buf) - 1] = '\0';
    prv_create_directory_recursive(base_dir_buf);

    prv_czip_emit_progress(progress_cb, "", processed, total_size, user_ctx);

    int ret = 0;
    while (1) {
        uint8_t flag = 0;
        if (fs_read(&flag, 1, 1, fd) != 1) {
            ret = -15;
            break;
        }
        processed += 1;

        if (flag == 0) {
            char name[APP_UNZIP_MAX_PATH] = {0};
            uint32_t consumed = 0;
            if (prv_czip_read_name(fd, name, sizeof(name), &consumed) < 0) {
                ret = -16;
                break;
            }
            processed += consumed;

            char full_path[APP_UNZIP_MAX_PATH * 2];
            snprintf(full_path, sizeof(full_path), "%s/%s", output_dir, name);
            prv_normalize_path(full_path);

            prv_create_directory_recursive(full_path);
            prv_czip_emit_progress(progress_cb, name, processed, total_size, user_ctx);
        } else if (flag == 1) {
            char name[APP_UNZIP_MAX_PATH] = {0};
            uint32_t consumed = 0;
            if (prv_czip_read_name(fd, name, sizeof(name), &consumed) < 0) {
                ret = -17;
                break;
            }
            processed += consumed;

            uint32_t file_size = 0;
            if (fs_read(&file_size, 1, 4, fd) != 4) {
                ret = -18;
                break;
            }
            processed += 4;

            char full_path[APP_UNZIP_MAX_PATH * 2];
            snprintf(full_path, sizeof(full_path), "%s/%s", output_dir, name);
            prv_normalize_path(full_path);
            prv_create_parent_directories(full_path);

            int out_fd = fs_open(full_path, "wb");
            if (out_fd <= 0) {
                ret = -19;
                break;
            }

            uint32_t remaining = file_size;
            while (remaining > 0) {
                size_t to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
                int read_len = (int)fs_read(buffer, 1, to_read, fd);
                if (read_len != (int)to_read) {
                    ret = -20;
                    break;
                }
                while (audio_resource_is_playing())
                {
                    osiThreadSleep(20);
                }
                size_t written = fs_write(buffer, 1, (size_t)read_len, out_fd);
                if (written != (size_t)read_len) {
                    ret = -21;
                    break;
                }
                remaining -= (uint32_t)read_len;
            }

            fs_close(out_fd);
            if (ret != 0) {
                break;
            }

            processed += file_size;
            prv_czip_emit_progress(progress_cb, name, processed, total_size, user_ctx);
        } else if (flag == 2) {
            uint8_t tail = 0;
            if (fs_read(&tail, 1, 1, fd) != 1) {
                ret = -22;
                break;
            }
            processed += 1;
            if (tail != 0xED) {
                ret = -23;
            }
            break;
        } else {
            ret = -24;
            break;
        }
    }

    fs_close(fd);

    if (ret == 0) {
        prv_czip_emit_progress(progress_cb, "", total_size, total_size, user_ctx);
    }
    return ret;
}
