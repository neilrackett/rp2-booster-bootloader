# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.26)

# if PICOTOOL_OVERRIDE_DIR system environment variable is set,
# then use that as the folder for the picotool executable
if (DEFINED ENV{PICOTOOL_OVERRIDE_DIR})
    message(STATUS "PICOTOOL_OVERRIDE_DIR env var is set to '$ENV{PICOTOOL_OVERRIDE_DIR}'")
    add_executable(picotool IMPORTED GLOBAL)
    set_property(TARGET picotool PROPERTY IMPORTED_LOCATION $ENV{PICOTOOL_OVERRIDE_DIR}/picotool)
    # check the picotool version:
    execute_process(COMMAND $ENV{PICOTOOL_OVERRIDE_DIR}/picotool version
                    OUTPUT_VARIABLE PICOTOOL_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^picotool v${picotool_VERSION_REQUIRED}" PICOTOOL_VERSION_MATCH ${PICOTOOL_VERSION})
    if (NOT PICOTOOL_VERSION_MATCH)
        message(STATUS "picotool version response was: ${PICOTOOL_VERSION}")
        message(FATAL_ERROR "PICOTOOL_OVERRIDE_DIR is set to '$ENV{PICOTOOL_OVERRIDE_DIR}', but the version of picotool found is not ${picotool_VERSION_REQUIRED}")
    else()
        message(STATUS "picotool version response was: ${PICOTOOL_VERSION}")
    endif()
endif ()

# If not specified, use pico_w as default board type
set(BOARD_TYPE $ENV{BOARD_TYPE})
if(NOT DEFINED BOARD_TYPE)
    message(STATUS "BOARD_TYPE not set, using default pico_w")
    set(BOARD_TYPE "pico")
endif()
set(PICO_BOARD ${BOARD_TYPE})

# Determine board type macros
if("${BOARD_TYPE}" STREQUAL "pico_w")
    add_compile_definitions(BOARD_TYPE_PICO_W=1 BOARD_TYPE_PICO=0 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "pico")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "sidecartos_16mb")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
else()
    message(FATAL_ERROR "Unknown BOARD_TYPE: ${BOARD_TYPE}")
endif()

# Add the env variable FATFS_SDK_PATH if it does not exist
if(NOT DEFINED ENV{FATFS_SDK_PATH})
    set(ENV{FATFS_SDK_PATH} ../../fatfs-sdk)
endif()
# Show the value
message(STATUS "FATFS_SDK_PATH: $ENV{FATFS_SDK_PATH}")

# Add the env variable PICO_SDK_PATH if it does not exist
if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} ../../pico-sdk)
endif()
# Show the value
message(STATUS "PICO_SDK_PATH: $ENV{PICO_SDK_PATH}")

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

# Set name of project (as PROJECT_NAME) and C/C   standards
project(upgrader C CXX)
set(CMAKE_C_STANDARD 11)
# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Tell CMake where to find other source code
add_subdirectory($ENV{FATFS_SDK_PATH}/src build)
  
# Build a minimal FatFs/SD stack for the upgrader only.
set(FATFS_SDK_SRC_DIR $ENV{FATFS_SDK_PATH}/src)

add_library(upgrader_fatfs STATIC
    ${FATFS_SDK_SRC_DIR}/ff15/source/ff.c
    ${FATFS_SDK_SRC_DIR}/ff15/source/ffunicode.c
    ${FATFS_SDK_SRC_DIR}/sd_driver/dma_interrupts.c
    ${FATFS_SDK_SRC_DIR}/sd_driver/sd_timeouts.c
    ${FATFS_SDK_SRC_DIR}/sd_driver/SPI/my_spi.c
    ${FATFS_SDK_SRC_DIR}/sd_driver/SPI/sd_card_spi.c
    ${FATFS_SDK_SRC_DIR}/sd_driver/SPI/sd_spi.c
    ${FATFS_SDK_SRC_DIR}/src/crc.c
    ${FATFS_SDK_SRC_DIR}/src/glue.c
    fatfs_sd_card.c
    my_debug.c
)

target_compile_definitions(upgrader_fatfs PUBLIC
    PICO_MAX_SHARED_IRQ_HANDLERS=8u
)

target_include_directories(upgrader_fatfs PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${FATFS_SDK_SRC_DIR}/ff15/source
    ${FATFS_SDK_SRC_DIR}/sd_driver
    ${FATFS_SDK_SRC_DIR}/include
)

target_link_libraries(upgrader_fatfs PUBLIC
    hardware_dma
    hardware_pio
    hardware_spi
    pico_stdlib
    cmsis_core
)

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME}
    main.c
)

target_sources(${PROJECT_NAME} PRIVATE
    gconfig.c
    hw_config.c
    # Booster's copy into flash, shared so the two write it the same way.
    ${CMAKE_CURRENT_LIST_DIR}/../../booster/src/uf2copy.c
)

# Ensure local headers are visible to all target sources. Booster's include
# folder comes after the upgrader's own, for uf2copy.h only: the upgrader has
# its own constants.h and debug.h, which must win.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../../booster/src/include
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

# Link to pico_stdlib (gpio, time, etc. functions)
# Base libraries
set(LINK_LIBRARIES
    upgrader_fatfs
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBRARIES}
    hardware_flash # Specific hardware flash library
)

# Fetch the values from the environment variables
set(RELEASE_TYPE $ENV{RELEASE_TYPE})
set(RELEASE_VERSION $ENV{RELEASE_VERSION})
set(RELEASE_DATE $ENV{RELEASE_DATE})
set(_DEBUG $ENV{DEBUG_MODE})

# If the environment variables are not set, use default values
if(NOT DEFINED RELEASE_VERSION)

    # Check if RELEASE_TYPE is set and not empty, otherwise use default "FINAL"
    if(NOT DEFINED RELEASE_TYPE OR RELEASE_TYPE STREQUAL "")
        set(RELEASE_TYPE "final")
    endif()

    # Determine the file name based on RELEASE_TYPE
    if(RELEASE_TYPE STREQUAL "final")
        set(VERSION_FILE_NAME "version.txt")
    else()
        set(VERSION_FILE_NAME "version-${RELEASE_TYPE}.txt")
    endif()
    
    # Remove newline character if present
    string(STRIP "${RELEASE_VERSION}" RELEASE_VERSION)

    # In case version.txt is empty or does not exist, use a default version
    if("${RELEASE_VERSION}" STREQUAL "")
        set(RELEASE_VERSION "v0.0.1dev")
    endif()
endif()

if(NOT DEFINED RELEASE_DATE)
        string(TIMESTAMP CURRENT_DATE_TIME "%Y-%m-%d %H:%M:%S")
        set(RELEASE_DATE ${CURRENT_DATE_TIME})
endif()

if (NOT DEFINED _DEBUG)
        set(_DEBUG 0)
endif()

if(${_DEBUG} STREQUAL "0")
    target_link_options(${PROJECT_NAME} PRIVATE
    "-Wl,--gc-sections"
    "-Wl,--strip-all"
    )

else()
    target_link_libraries(${PROJECT_NAME} PRIVATE pico_stdlib)
    pico_enable_stdio_uart(${PROJECT_NAME} 1)
    pico_enable_stdio_usb(${PROJECT_NAME} 0)
    # The same 921,600 baud as Booster's debug console, so one capture
    # (tools/dev/console.py) reads both across the reboot into the upgrader.
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_DEFAULT_UART_BAUD_RATE=921600)
endif()

# Pass these values to the C compiler
# DEBUG_MODE turns on the DPRINTF output in debug.h.
add_definitions(-D_DEBUG=${_DEBUG})
add_definitions(-DRELEASE_TYPE="${RELEASE_TYPE}")
add_definitions(-DRELEASE_VERSION="${RELEASE_VERSION}")
add_definitions(-DRELEASE_DATE="${RELEASE_DATE}")
add_definitions(-DLATEST_RELEASE_URL="${LATEST_RELEASE_URL}")

# Define your binary type
set(PICO_BINARY_TYPE copy_to_ram)  # Change to e.g. no_flash if needed

# Set binary type using the variable
pico_set_binary_type(${PROJECT_NAME} ${PICO_BINARY_TYPE})

# Conditionally set linker script
if(NOT "${PICO_BINARY_TYPE}" STREQUAL "copy_to_ram")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_upgrader.ld
    )
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_upgrader_copy_to_ram.ld
    )
endif()

# Print the variables on screen
message(STATUS "RELEASE_TYPE: " ${RELEASE_TYPE})
message(STATUS "RELEASE_VERSION: " ${RELEASE_VERSION})
message(STATUS "RELEASE_DATE: " ${RELEASE_DATE})
message(STATUS "DEBUG_MODE: " ${_DEBUG})
message(STATUS "LATEST_RELEASE_URL: " ${LATEST_RELEASE_URL})
message(STATUS "PICO_USE_FASTEST_SUPPORTED_CLOCK: " ${PICO_USE_FASTEST_SUPPORTED_CLOCK})
