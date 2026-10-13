# Copyright (C) 2018 ChinaiInfoSafe Technologies Limited and/or its affiliates("CIS").
# All rights reserved.
#
# This software is supplied "AS IS" without any warranties.
# CIS assumes no responsibility or liability for the use of the software,
# conveys no license or title under any patent, copyright, or mask work
# right to the product. CIS reserves the right to make changes in the
# software without notification.  CIS also make no representation or
# warranty that such application will be suitable for the specified use
# without further testing or modification.

set(target pahomqtt)
add_app_libraries($<TARGET_FILE:${target}>)

add_library(${target} STATIC)
set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${out_lib_dir})
# target_include_directories(${target} PUBLIC include)
target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(${target} PROPERTIES OUTPUT_NAME pahomqtt)

target_compile_definitions(${target} PRIVATE MBEDTLS)

target_include_directories(${target} PUBLIC src/paho.mqtt.c-master/src)
target_include_directories(${target} PRIVATE src/cis/adapter/posix/include)
target_include_directories(${target} PRIVATE src/cis/adapter/paho/include)

set(LIBPAHOMQTTDIR src)
set(PAHOMQTTFILES
    ${LIBPAHOMQTTDIR}/mqtt_list.c
    ${LIBPAHOMQTTDIR}/mqtt_sign.c
    ${LIBPAHOMQTTDIR}/mqttclient.c
    ${LIBPAHOMQTTDIR}/MQTTConnectClient.c
    ${LIBPAHOMQTTDIR}/MQTTConnectServer.c
    ${LIBPAHOMQTTDIR}/MQTTDeserializePublish.c
    ${LIBPAHOMQTTDIR}/MQTTFormat.c
    ${LIBPAHOMQTTDIR}/MQTTPacket.c
    ${LIBPAHOMQTTDIR}/MQTTSerializePublish.c
    ${LIBPAHOMQTTDIR}/MQTTSubscribeClient.c
    ${LIBPAHOMQTTDIR}/MQTTSubscribeServer.c
    ${LIBPAHOMQTTDIR}/MQTTUnsubscribeClient.c
    ${LIBPAHOMQTTDIR}/MQTTUnsubscribeServer.c
    ${LIBPAHOMQTTDIR}/nettype_tcp.c
    ${LIBPAHOMQTTDIR}/nettype_tls.c
    ${LIBPAHOMQTTDIR}/network.c
    ${LIBPAHOMQTTDIR}/platform_memory.c
    ${LIBPAHOMQTTDIR}/platform_mutex.c
    ${LIBPAHOMQTTDIR}/platform_net_socket.c    
    ${LIBPAHOMQTTDIR}/platform_thread.c
    ${LIBPAHOMQTTDIR}/platform_timer.c
    ${LIBPAHOMQTTDIR}/random.c
)

target_sources(${target} PRIVATE
    #${EXFILES}
    ${PAHOMQTTFILES}
    # ${UNITTESTFILES}
    # ${DEMOFILES}
    #${TESTFILES}
)
