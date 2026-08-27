picofuse_library(
    NAME picofuse-sys
    SOURCES
        init.c
        puts.c
        ../stub/debugf.c
        ../stub/panicf.c
        ../stub/printf.c
    )

# For picofuse/sys/*.h and runtime/*.h (picofuse-sys's own sources
# include these).
target_include_directories(picofuse-sys PRIVATE ${CMAKE_SOURCE_DIR}/include)
