cmake_minimum_required(VERSION 3.20)

# Logic if building for a Pico board
if(DEFINED PICO_BOARD)
    # Point to the shared boards header directory
    set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/include/boards")

    # Load the Pico SDK toolchain before project() selects compilers.
    include(third_party/pico-sdk/pico_sdk_init.cmake)
endif()

project(picofuse C CXX ASM)

# Logic if building for a Pico board
if(DEFINED PICO_BOARD)
    pico_sdk_init()
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/picosdk_bundle.cmake)
    add_compile_definitions(PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)
    add_compile_definitions(LIB_PICO_MULTICORE=1)
    set(_picosdk_targets
        hardware_gpio
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
    )
    if(PICO_CYW43_SUPPORTED)
        list(APPEND _picosdk_targets pico_cyw43_arch_none pico_lwip_nosys)
    endif()

    # Generate the combined Pico SDK bundle
    set(_pico_clib_interface_dir ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/src/rp2_common/pico_clib_interface)
    set(_pico_runtime_dir ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/src/rp2_common/pico_runtime)
    picofuse_add_pico_sdk_bundle(picosdk
        TARGETS ${_picosdk_targets}
        SOURCES
            ${_pico_runtime_dir}/runtime.c
            ${_pico_clib_interface_dir}/newlib_interface.c
            ${_pico_clib_interface_dir}/cxa_guard.c
            ${CMAKE_CURRENT_LIST_DIR}/src/runtime/pico/puts.c # Our own stub in place of newlib's puts()
            ${CMAKE_CURRENT_LIST_DIR}/src/runtime/pico/stdout.c
            ${CMAKE_CURRENT_LIST_DIR}/src/runtime/pico/mbedtls.c
        HEADER_DEPS pico_binary_info_headers pico_runtime_headers
    )

    # Add the combined pico SDK libraries target
    picofuse_add_pico_sdk_libs_target(pico_sdk_libs)
    target_include_directories(picosdk_static PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        # lwipopts.h and mbedtls_config.h are located via a bare filename
        # #include from vendored SDK code (lwIP's opt.h hardcodes
        # "lwipopts.h" with no macro indirection to redirect it), so
        # include/runtime itself must be on the search path.
        ${CMAKE_CURRENT_LIST_DIR}/include/runtime
    )
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
