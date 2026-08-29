picofuse_library(
    NAME picofuse-sys
    LIBRARIES picosdk
    HEADERS pico_stdlib_headers
    SOURCES
        init.c
        halt.c
        mutex.c
        cond.c
        waitgroup.c
        thread.c
        sync.c
        puts.c
        random.c
        timestamp.c
        sleep.c
        ../string/rune.c
        ../string/count.c
        ../string/valid.c
        ../string/isa.c
        ../string/tokenize.c
        ../string/runes.c
        ../string/compare.c
        ../string/case.c
        ../string/trim.c
        ../string/escape.c
        ../string/bool.c
        ../string/quoted.c
        ../string/int.c
        ../string/has.c
        ../string/scanner.c
        ../string/iostream.c
        ../iostream/iostream.c
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
