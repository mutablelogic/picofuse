picofuse_library(
    NAME picofuse-sys
    LIBRARIES picosdk
    HEADERS pico_stdlib_headers
    SOURCES
        init.c
        panicf.c
        puts.c
        timestamp.c
        sleep.c
        ../printf/debugf.c
        ../printf/mutex.c
        ../printf/printf.c
        ../printf/put.c
        ../printf/putc.c
        ../printf/putd.c
        ../printf/putf.c
        ../printf/puts.c
        ../printf/vprintf.c
        ../stub/mutex.c
)

target_include_directories(picofuse-sys PRIVATE ${CMAKE_SOURCE_DIR}/include)
