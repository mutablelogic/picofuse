picofuse_library(
    NAME picofuse-sys
    SOURCES
        ../posix/init.c
        ../posix/puts.c
        ../stub/debugf.c
        ../stub/panicf.c
        ../stub/printf.c
)

target_include_directories(picofuse-sys PRIVATE ${CMAKE_SOURCE_DIR}/include)
