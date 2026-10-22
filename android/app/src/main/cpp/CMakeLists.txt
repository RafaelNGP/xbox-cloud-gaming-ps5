cmake_minimum_required(VERSION 3.22.1)
project(psbox_android)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")

find_library(log-lib log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)

add_library(psbox SHARED
    "${ROOT_DIR}/src/platform/android/platform_android.cpp"
    "${ROOT_DIR}/src/media/hw_decoder_android.cpp"
)

target_include_directories(psbox PRIVATE
    "${ROOT_DIR}/src"
)

target_link_libraries(psbox
    ${log-lib}
    ${android-lib}
    ${mediandk-lib}
)
