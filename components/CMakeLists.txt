# All rights reserved.
#
add_subdirectory_if_exist(mbedtls)
add_subdirectory_if_exist(net)
add_subdirectory_if_exist(wifi)  # Depends on libc_wrap
add_subdirectory_if_exist(zlib)
add_subdirectory_if_exist(libc_wrap)  # Conflicts with newlib