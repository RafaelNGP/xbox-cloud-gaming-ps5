# mbedTLS compiled from source for both builds (without net_sockets, tests or
# programs), with the DTLS-SRTP extension WebRTC needs.

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
# Keep the install export so libdatachannel/libSRTP exports can reference it.
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL OFF CACHE BOOL "" FORCE)
set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/mbedtls_user_config.h" CACHE FILEPATH "" FORCE)

add_subdirectory(deps/mbedtls ${CMAKE_BINARY_DIR}/mbedtls EXCLUDE_FROM_ALL)

add_library(xc_mbedtls INTERFACE)
target_link_libraries(xc_mbedtls INTERFACE mbedtls mbedx509 mbedcrypto)
target_include_directories(xc_mbedtls INTERFACE deps/mbedtls/include)
