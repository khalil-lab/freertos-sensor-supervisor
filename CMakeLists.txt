cmake_minimum_required(VERSION 3.15)
project(sensor_supervisor C)

find_package(Threads REQUIRED)

set(FREERTOS_KERNEL_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreeRTOS-Kernel")

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/config")

set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)
set(FREERTOS_PORT "GCC_POSIX" CACHE STRING "" FORCE)

add_subdirectory("${FREERTOS_KERNEL_PATH}" FreeRTOS-Kernel)

add_executable(sensor_supervisor src/main.c)
target_link_libraries(sensor_supervisor PRIVATE
    freertos_kernel freertos_config Threads::Threads)
