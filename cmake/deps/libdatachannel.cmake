# libdatachannel (WebRTC: ICE via libjuice, DTLS-SRTP, SCTP data channels)
# built static on top of the vendored mbedTLS. Include after mbedtls.cmake.

set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_WARNINGS OFF CACHE BOOL "" FORCE)

# cmake/modules/FindMbedTLS.cmake maps find_package(MbedTLS) to deps/mbedtls.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
find_package(MbedTLS REQUIRED)

add_subdirectory(deps/libdatachannel ${CMAKE_BINARY_DIR}/libdatachannel EXCLUDE_FROM_ALL)
