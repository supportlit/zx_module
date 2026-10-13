set(target libwifi)
set(WIFI_PREBUILT_LIB ${CMAKE_CURRENT_LIST_DIR}/prebuilt/libwifi.a)

add_app_libraries($<TARGET_FILE:${target}>)

if(EXISTS ${WIFI_PREBUILT_LIB})
    message(STATUS "Using prebuilt wifi library: ${WIFI_PREBUILT_LIB}")
    add_library(${target} STATIC IMPORTED GLOBAL)
    set_target_properties(${target} PROPERTIES
        IMPORTED_LOCATION ${WIFI_PREBUILT_LIB}
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    )

    target_include_directories(${target} INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/inc
        ${CMAKE_CURRENT_LIST_DIR}/export_inc
        ${CMAKE_CURRENT_LIST_DIR}/inc/at_device
        ${CMAKE_CURRENT_LIST_DIR}/inc/at_frame
        ${CMAKE_CURRENT_LIST_DIR}/inc/hal
        ${CMAKE_CURRENT_LIST_DIR}/inc/sal
        ${CMAKE_CURRENT_LIST_DIR}/inc/esp_serial_flasher
        ${CMAKE_CURRENT_LIST_DIR}/../lwip/ipv4
    )

    target_compile_definitions(${target} INTERFACE
        CONFIG_MOD_WIFI=1
        CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET=1
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_SSID="NPWIFITEST2"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_PASSWORD="np72206220"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_IP="192.168.1.1"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_DHCP_IP_MIN="192.168.1.2"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_DHCP_IP_MAX="192.168.1.254"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_ESP8266=1
        CONFIG_LOSCFG_COMPONENTS_NET_AT_ESP8266_HZR_SPK=1
        LOSCFG_COMPONENTS_NET_AT_ESP8266=1
    )
else()
    add_library(${target} STATIC ${dummy_c_file})
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${out_lib_dir}
        OUTPUT_NAME wifi
    )

    target_link_libraries(${target} PRIVATE libc_wrap ${libc_file_name} ${libm_file_name} ${libgcc_file_name})

    target_compile_definitions(${target} PRIVATE
        CONFIG_MOD_WIFI=1
        CONFIG_LOSCFG_COMPONENTS_NET_AT_SOCKET=1
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_SSID="NPWIFITEST2"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_PASSWORD="np72206220"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_IP="192.168.1.1"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_DHCP_IP_MIN="192.168.1.2"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_DEFAULT_WIFI_AP_DHCP_IP_MAX="192.168.1.254"
        CONFIG_LOSCFG_COMPONENTS_NET_AT_ESP8266=1
        CONFIG_LOSCFG_COMPONENTS_NET_AT_ESP8266_HZR_SPK=1
        LOSCFG_COMPONENTS_NET_AT_ESP8266=1
    )

    file(GLOB_RECURSE NEW_SOURCES
        src/*.c
        src/esp_serial_flasher/*.c
        src/at_frame/*.c
        src/hal/*.c
        src/socket/*.c
    )

    set(ESP_NEW_SOURCES
        src/device/esp8266.c
        src/device/esp8266_gpio.c
    )
    list(APPEND NEW_SOURCES ${ESP_NEW_SOURCES})

    list(APPEND FILE_SOURCES ${NEW_SOURCES})
    target_sources(${target} PUBLIC
        ${FILE_SOURCES}
    )
endif()

if(NOT EXISTS ${WIFI_PREBUILT_LIB})
    target_include_directories(${target} PUBLIC inc)
    target_include_directories(${target} PUBLIC export_inc)
    target_include_directories(${target} PUBLIC inc/at_device)
    target_include_directories(${target} PUBLIC inc/at_frame)
    target_include_directories(${target} PUBLIC inc/hal)
    target_include_directories(${target} PUBLIC inc/sal)
    target_include_directories(${target} PUBLIC inc/esp_serial_flasher)
    target_include_directories(${target} PUBLIC ../lwip/ipv4)
endif()



