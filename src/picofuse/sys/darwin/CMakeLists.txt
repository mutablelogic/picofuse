picofuse_library(
    NAME picofuse-sys
    SOURCES
        ../posix/halt.c
        ../posix/init.c
        ../posix/puts.c
        ../posix/random.c
        ../posix/sleep.c
        ../posix/timestamp.c
        ../pthread/mutex.c
        ../pthread/cond.c
        ../printf/debugf.c
        ../printf/mutex.c
        ../printf/panicf.c
        ../printf/printf.c
        ../printf/put.c
        ../printf/putc.c
        ../printf/putd.c
        ../printf/putf.c
        ../printf/puts.c
        ../printf/vprintf.c
)

target_include_directories(picofuse-sys PRIVATE ${CMAKE_SOURCE_DIR}/include)
