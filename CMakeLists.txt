cmake_minimum_required(VERSION 3.20)

# Logic if building for a Pico board
if(DEFINED PICO_BOARD)
    set(PICO_SDK_PATH
        "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk"
        CACHE PATH "Path to the Pico SDK used for Pico builds")
    # Point to the shared boards header directory
    set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/include/boards")

    # Load the Pico SDK toolchain before project() selects compilers.
    include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
endif()

project(picofuse C CXX ASM)

# Logic if building for a Pico board
if(DEFINED PICO_BOARD)
    pico_sdk_init()
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/picosdk_bundle.cmake)
    add_compile_definitions(PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)
    set(_picosdk_targets
        hardware_adc
        hardware_dma
        hardware_gpio
        hardware_i2c
        hardware_spi
        hardware_uart
        pico_multicore
        pico_bit_ops
        pico_divider
        pico_double
        pico_int64_ops
        pico_float
        pico_malloc
        pico_mem_ops
        pico_mbedtls
        pico_atomic
        pico_cxx_options
        pico_standard_binary_info
        pico_standard_link
        pico_sync
        pico_rand
        pico_printf
        pico_crt0
        pico_runtime_init
        pico_thread_local
        pico_stdio_rtt
    )
    if(PICO_CYW43_SUPPORTED)
        list(APPEND _picosdk_targets pico_cyw43_arch_lwip_poll)
    endif()

    # Generate the combined Pico SDK bundle
    set(_pico_clib_interface_dir ${PICO_SDK_PATH}/src/rp2_common/pico_clib_interface)
    set(_pico_runtime_dir ${PICO_SDK_PATH}/src/rp2_common/pico_runtime)
    set(_pico_sdk_bundle_sources
        ${_pico_runtime_dir}/runtime.c
        ${_pico_clib_interface_dir}/newlib_interface.c
        ${CMAKE_CURRENT_LIST_DIR}/src/runtime/pico/stdout.c # Also stubs newlib's own puts()
        ${CMAKE_CURRENT_LIST_DIR}/src/runtime/pico/mbedtls.c
    )
    # cxa_guard.c was only added in Pico SDK 2.3.0; older SDKs don't need/have it.
    if(EXISTS ${_pico_clib_interface_dir}/cxa_guard.c)
        list(APPEND _pico_sdk_bundle_sources ${_pico_clib_interface_dir}/cxa_guard.c)
    endif()
    picofuse_add_pico_sdk_bundle(picosdk
        TARGETS ${_picosdk_targets}
        SOURCES ${_pico_sdk_bundle_sources}
        HEADER_DEPS pico_binary_info_headers pico_runtime_headers
    )

    # Add the combined pico SDK libraries target
    picofuse_add_pico_sdk_libs_target(pico_sdk_libs)
    target_include_directories(picosdk_static PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        # lwipopts.h and mbedtls_config.h are in
        # include/runtime itself must be on the search path.
        ${CMAKE_CURRENT_LIST_DIR}/include/runtime
    )

    if(PICO_CYW43_SUPPORTED)
        # printf support for cyw43: force-include a header that redirects
        # CYW43_PRINTF to sys_debugf()
        target_compile_options(picosdk_static PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/include/runtime/cyw43_debug.h>"
        )
    endif()
endif()

# Lowercase and uppercase system name
# SYSTEM_NAME_PICO // SYSTEM_NAME=pico
# SYSTEM_NAME_DARWIN // SYSTEM_NAME=darwin
# SYSTEM_NAME_LINUX // SYSTEM_NAME=linux
string(TOLOWER ${CMAKE_SYSTEM_NAME} SYSTEM_NAME_LOWER)
string(TOUPPER ${CMAKE_SYSTEM_NAME} SYSTEM_NAME_UPPER)
string(TOLOWER ${CMAKE_SYSTEM_PROCESSOR} PROCESSOR_NAME_LOWER)

# Set include directories for project headers
include(${CMAKE_CURRENT_LIST_DIR}/cmake/picosdk_executable.cmake)

# Compile options
add_compile_options(
    "-Wall"
    "-Wextra"
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:Release>:-DNDEBUG>"
    "-DSYSTEM_NAME_${SYSTEM_NAME_UPPER}"
    "-DSYSTEM_NAME=\"${SYSTEM_NAME_LOWER}\""
    "-DSYSTEM_PROCESSOR=\"${PROCESSOR_NAME_LOWER}\""
    "-DPICOFUSE_VERSION=\"${PROGRAM_VERSION}\""
)

# Libraries
add_subdirectory(src/picofuse)

# Examples
add_subdirectory(examples/helloworld)
add_subdirectory(examples/flags)
add_subdirectory(examples/stdin)

# Hardware test runner
add_subdirectory(src/test)

# Tests
include(CTest)
add_subdirectory("test")

# Install
# Installable package: lib/, include/, lib/pkgconfig/*.pc
#include(${CMAKE_CURRENT_LIST_DIR}/cmake/picosdk_install.cmake)
#picofuse_install_package(picosdk DESCRIPTION "picofuse pico-sdk runtime bundle")
#picofuse_install_package(picofuse_sys DESCRIPTION "picofuse-sys"
#    REQUIRES_PRIVATE picosdk
#    PUBLIC_HEADER_DIRS picofuse
#)
