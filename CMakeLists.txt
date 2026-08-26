cmake_minimum_required(VERSION 3.20)

set(PICO_BOARD "pico" CACHE STRING "pico-sdk board header to use")
include(${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/pico_sdk_init.cmake)
project(picofuse C CXX ASM)
pico_sdk_init()
include(${CMAKE_CURRENT_LIST_DIR}/cmake/picosdk_bundle.cmake)

# Switches sleep_ms from the IRQ/alarm-pool path to a plain
# busy-wait, dropping the pool's bookkeeping (default_alarm_pool,
# alarm_pool_add_alarm_at_force_in_context, ...).
add_compile_definitions(PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)
add_compile_definitions(LIB_PICO_MULTICORE=1)

# Create a combined libpicosdk.a archive
set(_pico_clib_interface_dir ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/src/rp2_common/pico_clib_interface)
set(_pico_runtime_dir ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/src/rp2_common/pico_runtime)

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
    pico_atomic
    pico_cxx_options
    pico_standard_binary_info
    pico_standard_link
    pico_sync
    pico_printf
    pico_crt0
    pico_runtime_init
    pico_thread_local
)
if(PICO_CYW43_SUPPORTED)
    list(APPEND _picosdk_targets pico_cyw43_arch_none pico_lwip_nosys)
endif()

picofuse_add_pico_sdk_bundle(picosdk
    TARGETS ${_picosdk_targets}
    SOURCES
        ${_pico_runtime_dir}/runtime.c
        ${_pico_clib_interface_dir}/newlib_interface.c
        ${_pico_clib_interface_dir}/cxa_guard.c
        ${CMAKE_CURRENT_LIST_DIR}/src/runtime/pico/puts.c # Our own stub in place of newlib's puts()
        ${CMAKE_CURRENT_LIST_DIR}/src/runtime/pico/stdout.c
    HEADER_DEPS pico_binary_info_headers pico_runtime_headers
)

# For picofuse/sys.h (src/runtime/pico/puts.c's own puts() forwards to
# sys_puts()) and, on CYW43 boards, lwipopts.h (needed by pico_lwip_nosys,
# folded into picosdk above, pulled in only to satisfy cyw43_ll.c's
# pbuf_copy_partial dependency).
target_include_directories(picosdk_static PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Add the combined pico SDK libraries target
picofuse_add_pico_sdk_libs_target(pico_sdk_libs)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/picosdk_executable.cmake)
add_subdirectory(src/picofuse/sys/pico)

# Some examples
#add_subdirectory(examples/blink)
add_subdirectory(examples/helloworld)

# Installable package: lib/, include/, lib/pkgconfig/*.pc
include(${CMAKE_CURRENT_LIST_DIR}/cmake/picosdk_install.cmake)
picofuse_install_package(picosdk DESCRIPTION "picofuse pico-sdk runtime bundle")
picofuse_install_package(picofuse_sys_pico DESCRIPTION "picofuse/sys, UART-backed"
    REQUIRES_PRIVATE picosdk
    PUBLIC_HEADER_DIRS picofuse
)
