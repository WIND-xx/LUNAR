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
set(CMAKE_PROJECT_NAME LUNAR_RTOS)

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
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-u _printf_float")
# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
set(PROJECT_SOURCES
    # BSP sources
    bsp/bsp_rtc.c
    bsp/bt401.c
    bsp/buzzer.c
    bsp/heat.c
    bsp/key.c
    bsp/led.c
    bsp/ntc.c
    
    # Tools sources
    tools/at_ctrl.c
    tools/crc16.c
    tools/pid.c
    tools/pwm_driver.c
    # tools/ring_buffer.c
    
    # App sources
    app/BufferProcess.c
    app/at_process.c
    app/heat_task.c
    app/key_task.c
    app/protocal.c
    app/task_init.c
    # app/at_command_manager.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})


# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    bsp
    app
    tools
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

