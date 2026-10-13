#ifndef _FRAMEWORK_UNZIP_H_
#define _FRAMEWORK_UNZIP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    FRAMEWORK_UNZIP_TYPE_GENERIC = 0,
    FRAMEWORK_UNZIP_TYPE_RESOURCES = 1,
    FRAMEWORK_UNZIP_TYPE_OTA = 2
} framework_unzip_type_t;

int framework_unzip_init(void);
void framework_unzip_deinit(void);
int framework_unzip_start(const char *zip_path,
                          const char *output_dir,
                          framework_unzip_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* _FRAMEWORK_UNZIP_H_ */
