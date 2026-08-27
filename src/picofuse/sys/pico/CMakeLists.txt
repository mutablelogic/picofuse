picofuse_library(
    NAME picofuse-sys
    LIBRARIES picosdk
    HEADERS pico_stdlib_headers
    SOURCES
        init.c
        puts.c
        ../stub/debugf.c
        ../stub/panicf.c
        ../stub/printf.c
)

target_include_directories(picofuse-sys PRIVATE ${CMAKE_SOURCE_DIR}/include)
