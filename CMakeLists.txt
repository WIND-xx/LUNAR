cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME LUNAR)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
set(PROJECT_SOURCES
    # BSP sources (v2.0 — unified interface, no OS dependency, handle pattern)
    bsp/bsp_bt401.c
    bsp/bsp_buzzer.c
    bsp/bsp_heat.c
    bsp/bsp_key.c
    bsp/bsp_led.c
    bsp/bsp_ntc.c
    bsp/bsp_pwm.c
    bsp/bsp_rtc.c
    
    # Components - PID
    components/pid/pid.c
    
    # Components - CRC16
    components/crc16/crc16.c
    
    # Components - Event Bus
    components/event_bus/event_bus.c
    
    # Components - Protocol
    components/protocol/modbus_slave.c
    components/protocol/at_parser.c
    components/protocol/at_command_manager.c
    
    # Service sources
    app/service/key_service.c
    app/service/heat_service.c
    app/service/ble_service.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})

# Add app_main separately
set(APP_MAIN app/app_main.c)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${APP_MAIN})


# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    bsp
    app
    app/service
    components/pid
    components/crc16
    components/event_bus
    components/protocol
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# 生成 HEX 和 BIN 文件
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    BYPRODUCTS ${CMAKE_PROJECT_NAME}.hex ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating HEX and binary files"
)

# target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
