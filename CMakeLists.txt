# All rights reserved.
#
cmake_minimum_required(VERSION 3.13)
set(SOURCE_TOP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BINARY_TOP_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Ensure CUSTOMER_NAME is provided either via -D or environment
if(NOT DEFINED CUSTOMER_NAME OR "${CUSTOMER_NAME}" STREQUAL "")
    if(DEFINED ENV{CUSTOMER_NAME} AND NOT "$ENV{CUSTOMER_NAME}" STREQUAL "")
        set(CUSTOMER_NAME $ENV{CUSTOMER_NAME})
        message(STATUS "CUSTOMER_NAME not passed to CMake, using environment value: ${CUSTOMER_NAME}")
    else()
        message(FATAL_ERROR "CUSTOMER_NAME is not defined. Pass -DCUSTOMER_NAME=<name> or set the CUSTOMER_NAME environment variable before running CMake.")
    endif()
endif()

set(CUSTOMER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/customers/${CUSTOMER_NAME})
if(NOT IS_DIRECTORY ${CUSTOMER_DIR})
    message(FATAL_ERROR "Customer directory not found: ${CUSTOMER_DIR}")
endif()
message(STATUS "Using customer directory: ${CUSTOMER_DIR}")

# Optional per-customer build config to toggle framework modules or add flags
set(CUSTOMER_BUILD_CONFIG ${CUSTOMER_DIR}/customer_build_config.cmake)
if(EXISTS ${CUSTOMER_BUILD_CONFIG})
    message(STATUS "Including customer build config: ${CUSTOMER_BUILD_CONFIG}")
    include(${CUSTOMER_BUILD_CONFIG})
else()
    message(STATUS "No customer build config found for ${CUSTOMER_NAME}, using defaults")
endif()
set(CMAKE_C_COMPILER_FORCED   1)
set(CMAKE_CXX_COMPILER_FORCED 1)
set(WITH_THUMB_ISA 1)
# set(WITH_WERROR 1)
if($ENV{VFP_EN} MATCHES y)
    set(WITH_HARD_VFP 1)
endif()
set(out_hex_dir ${CMAKE_CURRENT_BINARY_DIR}/hex)
set(out_lib_dir ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(out_inc_dir ${CMAKE_CURRENT_BINARY_DIR}/include)
set(tools_dir ${CMAKE_CURRENT_SOURCE_DIR}/tools)
set(aboot_dir  ${SOURCE_TOP_DIR}/../../Tools/aboot)
set(module_version $ENV{module_version})
set(KERNEL_BASE_DEFAULT ${CMAKE_CURRENT_SOURCE_DIR}/../kernel)
set(KERNEL_BASE_CUSTOMER ${CUSTOMER_DIR}/kernel)
if(DEFINED ENV{KERNEL_BASE} AND NOT "$ENV{KERNEL_BASE}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{KERNEL_BASE}" KERNEL_BASE)
elseif(IS_DIRECTORY ${KERNEL_BASE_CUSTOMER})
    set(KERNEL_BASE ${KERNEL_BASE_CUSTOMER})
else()
    set(KERNEL_BASE ${KERNEL_BASE_DEFAULT})
endif()
if(NOT IS_DIRECTORY ${KERNEL_BASE}/${module_version})
    message(FATAL_ERROR "Kernel directory not found: ${KERNEL_BASE}/${module_version}")
endif()
message(STATUS "Using kernel base: ${KERNEL_BASE}")
set(core_stub_o ${KERNEL_BASE}/${module_version}/core_stub/core_stub.o)
set(flash_ldscript ${SOURCE_TOP_DIR}/ldscripts/app_flashimg.ld)
set(cmd_mkappimg dtools mkappimg)
set(ctool_sign ctools appSign)
include(cmake/core_config.cmake)
include(cmake/toolchain-gcc.cmake)
include(cmake/extension.cmake)
set(BUILD_SHARED_LIBS OFF)
project(core C CXX ASM)
if($ENV{para_flash_size} MATCHES "16")
	add_definitions(-DBOARD_FLASH_SIZE_16M)
elseif($ENV{para_flash_size} MATCHES "8")
    add_definitions(-DBOARD_FLASH_SIZE_8M)
elseif($ENV{para_flash_size} MATCHES "4")
    set(flash_ldscript ${SOURCE_TOP_DIR}/ldscripts/app_flashimg_xip.ld)
    add_definitions(-DBOARD_FLASH_SIZE_4M)
endif()
if(${WITH_THUMB_ISA} MATCHES 1)
    add_definitions(-DCONFIG_THUMB_ISA)
endif()
add_definitions(-DMODULE_${module_version})
add_definitions(-DLWIP_COMPAT_SOCKETS=0)
add_definitions(-D_USE_LONG_TIME_T)

set(GAIN_V1 OFF)
if($ENV{GAIN_V1} MATCHES "y")
set(GAIN_V1 ON)
add_definitions(-DGAIN_V1)
endif()

include_directories(
    components/inc
    ${KERNEL_BASE}/${module_version}/inc
    components/newlib/include
    components/lwip
    components/lwip/lwip
    components/lwip/arch
    components/lwip/ipv4
    components/lwip/ipv6
    components/lwip/netif
    components/mbedtls/include
    components/cislvgl/include
    components/net/pahomqtt/inc
    components/net/json/cJSON
    components/net/httpclient/inc
    components/inc/lcd
    components/wifi/export_inc
    components/zlib
    components/zlib/contrib/minizip
    components/libc_wrap

    # components/libc_wrap  # Disabled - conflicts with newlib
    # Framework includes
    framework/inc
    framework/common
    framework/device
    framework/factory
    # soundbox_core  # Temporarily disabled
    # Customer includes
    customers/${CUSTOMER_NAME}
    customers/${CUSTOMER_NAME}/inc
)
function(cpp_only target file)
    add_library(${target} OBJECT ${file})
    set_source_files_properties(${file} PROPERTIES LANGUAGE C)
    target_compile_options(${target} PRIVATE -E -P -x c)
endfunction()
function(add_appimg target ldscript)
    set(gen_ldscript ${target}_ldscript)
    set(target_map_file ${out_hex_dir}/${target}.map)
    set(target_img_file ${out_hex_dir}/${target}.bin)
    cpp_only(${gen_ldscript} ${ldscript})
    add_executable(${target} ${ARGN} ${core_stub_o})
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${out_hex_dir})
    target_link_libraries(${target} PRIVATE -T $<TARGET_OBJECTS:${gen_ldscript}>)
    target_link_libraries(${target} PRIVATE -Wl,-Map=${target_map_file} -nostdlib -Wl,--gc-sections)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${cmd_mkappimg} $<TARGET_FILE:${target}> ${target_img_file}
        BYPRODUCTS ${target_img_file} ${target_map_file}
    )
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${ctool_sign} -p ASR -t ${aboot_dir} ${target_img_file}
        DEPENDS ${target_img_file}
        WORKING_DIRECTORY ${SOURCE_TOP_DIR}
    )
endfunction()
message("Board Flash Size: $ENV{para_flash_size}M")

if($ENV{RELEASE_MODE} MATCHES "ENABLE_RELEASE")
    set(RELEASE_MODE $ENV{RELEASE_MODE})
    add_definitions(-DRELEASE_MODE=${RELEASE_MODE})
    add_definitions(-DLOG_OUTPUT_USB)
    message("BUILD Release: ${RELEASE_MODE} (LOG_OUTPUT_USB)")
endif()

add_subdirectory_if_exist(components)
# add_subdirectory_if_exist(app)  # Disabled - paytm API needs migration

function(_fw_default flag default_value)
    if(NOT DEFINED ${flag})
        set(${flag} ${default_value} PARENT_SCOPE)
    endif()
endfunction()

# Default framework module toggles (can be overridden in customer_build_config.cmake)
_fw_default(FRAMEWORK_ENABLE_MSG            ON)
_fw_default(FRAMEWORK_ENABLE_MAIN           ON)
_fw_default(FRAMEWORK_ENABLE_AUDIO          ON)
_fw_default(FRAMEWORK_ENABLE_HTTP           ON)
_fw_default(FRAMEWORK_ENABLE_UNZIP          ON)
_fw_default(FRAMEWORK_ENABLE_KEY            ON)
_fw_default(FRAMEWORK_ENABLE_BATTERY        ON)
_fw_default(FRAMEWORK_ENABLE_LED            ON)
_fw_default(FRAMEWORK_ENABLE_MQTT           ON)
_fw_default(FRAMEWORK_ENABLE_NETWORK        ON)
_fw_default(FRAMEWORK_ENABLE_DEVICE_CONFIG  ON)
_fw_default(FRAMEWORK_ENABLE_CHARGER        ON)
_fw_default(FRAMEWORK_ENABLE_BMIC           OFF)
_fw_default(FRAMEWORK_ENABLE_FACTORY        ON)
_fw_default(FRAMEWORK_ENABLE_LOG            ON)
_fw_default(FRAMEWORK_ENABLE_LCD            ON)

set(FRAMEWORK_SOURCES "")
function(_fw_append name flag path)
    if(${flag})
        list(APPEND FRAMEWORK_SOURCES ${path})
        set(FRAMEWORK_SOURCES ${FRAMEWORK_SOURCES} PARENT_SCOPE)
    else()
        message(STATUS "Framework module ${name} disabled by config")
    endif()
endfunction()

_fw_append(msg           FRAMEWORK_ENABLE_MSG           framework/common/framework_msg.c)
_fw_append(main          FRAMEWORK_ENABLE_MAIN          framework/common/framework_main.c)
_fw_append(audio         FRAMEWORK_ENABLE_AUDIO         framework/common/framework_audio.c)
_fw_append(http          FRAMEWORK_ENABLE_HTTP          framework/common/framework_http.c)
_fw_append(unzip         FRAMEWORK_ENABLE_UNZIP         framework/common/framework_unzip.c)
_fw_append(key           FRAMEWORK_ENABLE_KEY           framework/device/framework_key.c)
_fw_append(battery       FRAMEWORK_ENABLE_BATTERY       framework/device/framework_battery.c)
_fw_append(led           FRAMEWORK_ENABLE_LED           framework/device/framework_led.c)
_fw_append(mqtt          FRAMEWORK_ENABLE_MQTT          framework/device/framework_mqtt.c)
_fw_append(network       FRAMEWORK_ENABLE_NETWORK       framework/device/framework_network.c)
_fw_append(device_config FRAMEWORK_ENABLE_DEVICE_CONFIG framework/device/framework_device_config.c)
_fw_append(charger       FRAMEWORK_ENABLE_CHARGER       framework/device/framework_charger.c)
_fw_append(bmic          FRAMEWORK_ENABLE_BMIC          framework/device/framework_bmic.c)
_fw_append(factory       FRAMEWORK_ENABLE_FACTORY       framework/device/framework_factory.c)
_fw_append(log           FRAMEWORK_ENABLE_LOG           framework/common/framework_log.c)
_fw_append(factory_at    FRAMEWORK_ENABLE_FACTORY       framework/factory/factory_at_core.c)
_fw_append(factory_at    FRAMEWORK_ENABLE_FACTORY       framework/factory/factory_at_cmds.c)
_fw_append(lcd           FRAMEWORK_ENABLE_LCD           framework/common/framework_lcd7567.c)
_fw_append(lcd           FRAMEWORK_ENABLE_LCD           framework/common/framework_lcd9853.c)
_fw_append(lcd           FRAMEWORK_ENABLE_LCD           framework/common/framework_lcd9853_paint.c)

if(FRAMEWORK_ENABLE_LED)
    add_definitions(-DFRAMEWORK_ENABLE_LED)
endif()

# Auto-generate audio resource map from CSV if needed
set(CUSTOMER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/customers/${CUSTOMER_NAME})
set(AUDIO_CSV ${CUSTOMER_DIR}/audio_resources.csv)
set(AUDIO_HEADER ${CUSTOMER_DIR}/audio_resource_map.h)
set(GEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_audio_resources_v2.py)

if(EXISTS ${AUDIO_CSV})
    # Find Python interpreter
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        # Add custom command to generate audio resource map when CSV changes
        # V2 script generates dual-layer architecture
        add_custom_command(
            OUTPUT ${AUDIO_HEADER}
            COMMAND ${Python3_EXECUTABLE} ${GEN_SCRIPT} ${AUDIO_CSV} ${AUDIO_HEADER} ${CUSTOMER_NAME}
            DEPENDS ${AUDIO_CSV} ${GEN_SCRIPT}
            COMMENT "Generating dual-layer audio resource map for ${CUSTOMER_NAME} from CSV"
            VERBATIM
        )
        
        # Create a custom target that depends on the generated header
        add_custom_target(generate_audio_resources
            DEPENDS ${AUDIO_HEADER}
        )
        
        message(STATUS "Audio resource auto-generation enabled for ${CUSTOMER_NAME} (V2 dual-layer)")
        message(STATUS "  CSV: ${AUDIO_CSV}")
        message(STATUS "  Header: ${AUDIO_HEADER}")
    else()
        message(WARNING "Python3 not found - audio resource map will not be auto-generated")
        message(WARNING "Please run generate_audio_map.bat manually if CSV changes")
    endif()
else()
    message(STATUS "No audio_resources.csv found for ${CUSTOMER_NAME}, skipping auto-generation")
endif()

set(CUSTOMER_SOURCES "")
if(EXISTS ${CUSTOMER_DIR}/CMakeLists.txt)
    add_subdirectory(${CUSTOMER_DIR} ${CMAKE_CURRENT_BINARY_DIR}/customer)
    message(STATUS "Customer CMakeLists loaded from ${CUSTOMER_DIR}/CMakeLists.txt")
elseif(EXISTS ${CUSTOMER_DIR}/src)
    file(GLOB CUSTOMER_SOURCES ${CUSTOMER_DIR}/src/*.c)
    message("Customer sources (glob): ${CUSTOMER_SOURCES}")
else()
    message(WARNING "No customer sources found for ${CUSTOMER_NAME}")
endif()

set(target customer_app)
add_appimg(${target} ${flash_ldscript}  
    examples/soundbox.c
    ${FRAMEWORK_SOURCES}
    ${CUSTOMER_SOURCES}
)

# Add dependency on audio resource generation if enabled
if(TARGET generate_audio_resources)
    add_dependencies(${target} generate_audio_resources)
    message(STATUS "Audio resource auto-generation linked to build target")
endif()
target_link_libraries(${target} PRIVATE zlibstatic pahomqtt httpclient json libwifi libc_wrap)