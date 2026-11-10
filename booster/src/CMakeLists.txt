# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.26)

find_package(Perl)
if(NOT PERL_FOUND)
    message(FATAL_ERROR "Perl is needed for generating the fsdata.c file")
endif()

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
    set(BOARD_TYPE "pico_w")
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

# Add the env variable PICO_EXTRAS_PATH if it does not exist
if(NOT DEFINED ENV{PICO_EXTRAS_PATH})
    set(ENV{PICO_EXTRAS_PATH} ../../pico-extras)
endif()
# Show the value
message(STATUS "PICO_EXTRAS_PATH: $ENV{PICO_EXTRAS_PATH}")

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

# Set name of project (as PROJECT_NAME) and C/C   standards
project(booster C CXX)
set(CMAKE_C_STANDARD 11)
# malloc must return NULL on failure instead of panicking.
#
# MUST be set before pico_sdk_init(): check_alloc() lives in the SDK's own
# malloc.c, and pico_sdk_init() pulls the SDK in as a subdirectory, which
# inherits compile definitions as they stand at that moment. Setting this
# further down the file compiles OUR sources with it and leaves the SDK's
# malloc.c untouched, which looks right and does nothing.
#
# The SDK default (PICO_MALLOC_PANIC 1) turns a failed allocation into
# panic("Out of memory"), which defeats libraries written to handle NULL and
# recover. FatFs's dir_clear() is the case that bit us: creating a directory
# asks for up to 32KB and HALVES the request until it fits, falling back to
# single-sector writes if nothing can be allocated. With the panic enabled the
# first oversized request killed the device, so booting with an SD card that
# has no apps folder was fatal. mbedTLS is written the same way and fails a
# handshake on a failed allocation rather than taking the device down.
#
# Every malloc in this firmware checks its result, and sbrk still refuses to
# grow the heap past __StackLimit, so the guard this removes was redundant.
if (NOT DEFINED ENV{PICO_MALLOC_PANIC})
    set(ENV{PICO_MALLOC_PANIC} "0")
endif()
add_compile_definitions(PICO_MALLOC_PANIC=$ENV{PICO_MALLOC_PANIC})

# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Create the fsdata.c file
set(MAKE_FS_DATA_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/external/makefsdata)
set(GENERATE_FS_DATA_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/external/generate_fsdata.pl)
set(FSDATA_WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/fsdata_work)
set(FLASH_USAGE_REPORT_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/report_flash_usage.cmake)
set(BOOSTER_SIZE_TOOL ${CMAKE_SIZE_UTIL})
if (NOT BOOSTER_SIZE_TOOL)
    find_program(BOOSTER_SIZE_TOOL NAMES arm-none-eabi-size)
endif()
file(GLOB_RECURSE BOOSTER_FS_FILES LIST_DIRECTORIES false
        ${CMAKE_CURRENT_LIST_DIR}/fs/*)

# WARNING: I use a modified version of the makefsdata script to add text/css headers, not supported in the original tool
if (NOT EXISTS ${MAKE_FS_DATA_SCRIPT})
        file(DOWNLOAD
                https://github.com/sidecartridge/lwip/blob/c476c83fd7fda8befd4fb3f99bac99cb2f31e9fd/src/apps/http/makefsdata/makefsdata
                ${MAKE_FS_DATA_SCRIPT}
                )
endif()

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c
        COMMAND
                perl ${GENERATE_FS_DATA_SCRIPT}
                ${MAKE_FS_DATA_SCRIPT}
                ${CMAKE_CURRENT_LIST_DIR}/fs
                ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c
                ${FSDATA_WORK_DIR}
        DEPENDS
                ${GENERATE_FS_DATA_SCRIPT}
                ${MAKE_FS_DATA_SCRIPT}
                ${BOOSTER_FS_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMENT "Minifying web assets and generating fsdata_srv.c"
        VERBATIM
)
add_custom_target(generate_booster_fsdata DEPENDS ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c)

# Add the settings library
add_subdirectory(settings)

# Add u8g2 library
add_subdirectory(u8g2)

# Add qrcodegen library
add_subdirectory(qrcodegen)

# Add cJSON library
add_subdirectory(cjson)

# Add MD5 library
add_subdirectory(md5)

# Tell CMake where to find other source code
add_subdirectory($ENV{FATFS_SDK_PATH}/src build)
  
# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME} 
        main.c
)

# A function that can end without returning its value is an error, not a
# warning: a CGI handler that fell off its end handed lwIP whatever was in r0
# as the page to send.
target_compile_options(${PROJECT_NAME} PRIVATE -Werror=return-type)

add_dependencies(${PROJECT_NAME} generate_booster_fsdata)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/romemul.pio)

target_sources(${PROJECT_NAME} PRIVATE
        appmngr.c
        blink.c
        cgiparam.c
        devapi.c
        display.c
        display_fabric.c
        display_mngr.c
        display_term.c
        fabric.c
        fabric_httpd.c
        hw_config.c
        httphdr.c
        gconfig.c
        jsonfield.c
        lookupbackup.c
        lookuptable.c
        lwipopts.h
        mngr.c
        mngr_httpd.c
        network.c
        network_parse.c
        reset.c
        romemul.c
        sdcard.c
        select.c
        term.c
        textescape.c
        uf2copy.c
        urlparse.c
        utc_time.c
        uuid.c
        vercmp.c
        version.c
        wificonf.c
        wificonf_parse.c
        cjson/cJSON.c
        httpc/httpc.c
        md5/md5.c
        settings/settings.c
        dhcpserver/dhcpserver.c
        dnsserver/dnsserver.c)

# mbedTLS needs a monotonic ms clock (MBEDTLS_PLATFORM_MS_TIME_ALT).
target_sources(${PROJECT_NAME} PRIVATE mbedtls_ms_time.c)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

if (BOOSTER_SIZE_TOOL)
    add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND
                    ${CMAKE_COMMAND}
                    -DREPORT_ELF=$<TARGET_FILE:${PROJECT_NAME}>
                    -DLINKER_SCRIPT=${CMAKE_CURRENT_LIST_DIR}/memmap_booster.ld
                    -DSIZE_TOOL=${BOOSTER_SIZE_TOOL}
                    -P ${FLASH_USAGE_REPORT_SCRIPT}
            VERBATIM
    )
else()
    message(WARNING "arm-none-eabi-size not found. Booster flash usage report disabled.")
endif()

# Link libraries required for the project
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${LINK_LIBRARIES}        # External or additional libraries passed as variables
    hardware_flash           # Flash memory access
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico                # FATFS library   
    pico_lwip                # lwIP library
    pico_lwip_http           # lwIP httpd library
    pico_lwip_mdns           # lwIP mDNS library
    pico_lwip_mbedtls        # lwIP ALTCP-over-mbedTLS glue
    pico_mbedtls             # mbedTLS library
    pico_multicore           # Multicore support
    pico_stdlib              # Core functionality
    qrcodegen                # QR code generation library
    settings                 # Custom settings library
    u8g2                     # for display
)

# Conditionally link the CYW43 architecture library if supported
if (PICO_CYW43_SUPPORTED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
#        pico_cyw43_arch_lwip_poll
        pico_cyw43_arch_lwip_threadsafe_background
        )
else()
    message(WARNING "CYW43 architecture not supported")
endif()

# Link custom memmap with reserved memory for ROMs
set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_booster.ld
)

# Needed to include lwipopts.h properly
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/httpc
    ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
    ${CMAKE_CURRENT_LIST_DIR}/dnsserver)


# Use reduced space for ROMs
pico_set_binary_type(${PROJECT_NAME} booster)

# Fetch the values from the environment variables
set(RELEASE_TYPE $ENV{RELEASE_TYPE})
set(RELEASE_VERSION $ENV{RELEASE_VERSION})
set(RELEASE_DATE $ENV{RELEASE_DATE})
set(_DEBUG $ENV{DEBUG_MODE})
set(DISPLAY_ATARIST $ENV{DISPLAY_ATARIST})
set(PICO_FLASH_ASSUME_CORE0_SAFE $ENV{PICO_FLASH_ASSUME_CORE0_SAFE})

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

# Debug outputs
pico_enable_stdio_usb(${PROJECT_NAME} 0)
# Workaround to disable USB output in release builds
if(${_DEBUG} STREQUAL "0")
    pico_enable_stdio_uart(${PROJECT_NAME} 0)
    # Remove unused code and data. The ELF keeps its symbol table: symbols
    # never reach the .uf2, which is byte-identical either way, and
    # tools/dev/swd.py reads the device by symbol name.
    target_link_options(${PROJECT_NAME} PRIVATE
    "-Wl,--gc-sections"
    )

else()
    pico_enable_stdio_uart(${PROJECT_NAME} 1)
    # 921,600 baud, not the SDK's 115,200: DPRINTF blocks on the UART, and on a
    # timing-sensitive cartridge bus a chatty console at 115,200 delays the
    # firmware enough to change behavior. The UART clock error at this rate is
    # 0.02 %. Set the serial terminal (tools/dev/console.py) to 921600.
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_DEFAULT_UART_BAUD_RATE=921600)
endif()

# Pass these values to the C compiler
add_definitions(-DRELEASE_TYPE="${RELEASE_TYPE}")
add_definitions(-DRELEASE_VERSION="${RELEASE_VERSION}")
add_definitions(-DRELEASE_DATE="${RELEASE_DATE}")
add_definitions(-DLATEST_RELEASE_URL="${LATEST_RELEASE_URL}")

# Pass the _DEBUG flag to the C compiler
add_definitions(-D_DEBUG=${_DEBUG})

# build_id.h and build_id.c (RELEASE_BUILD_ID and the release_build_id symbol,
# the git commit) are regenerated on every build; see build_id.cmake. They are
# rewritten only when the ID changes, so only their users recompile. The symbol
# is kept through --gc-sections so tools/dev/swd.py can read over SWD which
# build the device runs.
set(BUILD_ID_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/build_id)
add_custom_target(build_id
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_LIST_DIR}
            -DOUT_DIR=${BUILD_ID_DIR} -DDEBUG_BUILD=${_DEBUG}
            -DCMAKE_TYPE=${CMAKE_BUILD_TYPE}
            -P ${CMAKE_CURRENT_LIST_DIR}/build_id.cmake
    BYPRODUCTS ${BUILD_ID_DIR}/build_id.h ${BUILD_ID_DIR}/build_id.c
    COMMENT "Checking the build ID")
add_dependencies(${PROJECT_NAME} build_id)
target_sources(${PROJECT_NAME} PRIVATE ${BUILD_ID_DIR}/build_id.c)
set_source_files_properties(${BUILD_ID_DIR}/build_id.c PROPERTIES GENERATED TRUE)
target_include_directories(${PROJECT_NAME} PRIVATE ${BUILD_ID_DIR})
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--undefined=release_build_id")

# Device/Computer type
if (NOT DEFINED ENV{DISPLAY_ATARIST})
    set(ENV{DISPLAY_ATARIST} "1")  # Default to 1 if not set
endif()
add_definitions(-DDISPLAY_ATARIST=$ENV{DISPLAY_ATARIST})

# We don't modify the flash in Core 1
if (NOT DEFINED ENV{PICO_FLASH_ASSUME_CORE0_SAFE})
    set(ENV{PICO_FLASH_ASSUME_CORE0_SAFE} "1")
endif()
add_definitions(-DPICO_FLASH_ASSUME_CORE0_SAFE=$ENV{PICO_FLASH_ASSUME_CORE0_SAFE})

# PICO_DEOPTIMIZE_DEBUG is used to disable optimizations in debug mode
#if (NOT DEFINED ENV{PICO_DEOPTIMIZE_DEBUG})
#    set(ENV{PICO_DEOPTIMIZE_DEBUG} "1")
#endif()

# PICO_USE_FASTEST_SUPPORTED_CLOCK to the maximum frequency
if (NOT DEFINED ENV{PICO_USE_FASTEST_SUPPORTED_CLOCK})
    add_definitions(-DPICO_USE_FASTEST_SUPPORTED_CLOCK=1)
endif()

# Print the variables on screen
message(STATUS "RELEASE_TYPE: " ${RELEASE_TYPE})
message(STATUS "RELEASE_VERSION: " ${RELEASE_VERSION})
message(STATUS "RELEASE_DATE: " ${RELEASE_DATE})
message(STATUS "DEBUG_MODE: " ${_DEBUG})
message(STATUS "LATEST_RELEASE_URL: " ${LATEST_RELEASE_URL})
message(STATUS "DISPLAY_ATARIST: " ${DISPLAY_ATARIST})
message(STATUS "PICO_FLASH_ASSUME_CORE0_SAFE: " ${PICO_FLASH_ASSUME_CORE0_SAFE})
message(STATUS "PICO_USE_FASTEST_SUPPORTED_CLOCK: " ${PICO_USE_FASTEST_SUPPORTED_CLOCK})
#message(STATUS "PICO_DEOPTIMIZE_DEBUG: $ENV{PICO_DEOPTIMIZE_DEBUG}")

# Handle custom build type by checking if it's set
if(CMAKE_BUILD_TYPE STREQUAL "CustomBuild")
    # Define custom build type flags
    set(CMAKE_C_FLAGS_CUSTOMBUILD "-ffunction-sections -fdata-sections -O0 -mcpu=cortex-m0 -mthumb")
    set(CMAKE_CXX_FLAGS_CUSTOMBUILD "-ffunction-sections -fdata-sections -O0 -mcpu=cortex-m0 -mthumb")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_CUSTOMBUILD}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_CUSTOMBUILD}")
endif()
