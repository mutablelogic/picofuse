# Concrete pico (UART-backed) implementation of picofuse/sys.
picofuse_library(
    NAME picofuse_sys_pico
    LIBRARIES picosdk
    HEADERS pico_stdlib_headers
    SOURCES
        init.c
        puts.c
)

# For picofuse/sys/*.h and runtime/*.h (picofuse_sys_pico's own sources
# include these).
target_include_directories(picofuse_sys_pico_static PRIVATE ${CMAKE_SOURCE_DIR}/include)
