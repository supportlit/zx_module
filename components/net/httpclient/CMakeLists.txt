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

set(target httpclient)
add_app_libraries($<TARGET_FILE:${target}>)

add_library(${target} STATIC)
set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${out_lib_dir})
# target_include_directories(${target} PUBLIC include)
target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(${target} PROPERTIES OUTPUT_NAME httpclient)

target_include_directories(${target} PUBLIC inc)

set(HTTPCLIENTFILES
    src/httpclient.c
)

target_sources(${target} PRIVATE
    ${HTTPCLIENTFILES}
)
