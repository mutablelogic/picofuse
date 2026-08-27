picofuse_library(
    NAME picofuse-sys
    SOURCES
        ../posix/init.c
        ../posix/puts.c
        ../pthread/mutex.c
        ../printf/debugf.c
        ../printf/mutex.c
        ../printf/printf.c
        ../printf/put.c
        ../printf/putc.c
        ../printf/putd.c
        ../printf/putf.c
        ../printf/puts.c
        ../printf/vprintf.c
        ../posix/panicf.c
)

target_include_directories(picofuse-sys PRIVATE ${CMAKE_SOURCE_DIR}/include)
