set_property(GLOBAL PROPERTY PICOFUSE_HASH_BACKEND mbedtls)

picofuse_library(
    NAME picofuse-sys
    LIBRARIES picosdk
    HEADERS pico_stdlib_headers
    SOURCES
        init.c
        env.c
        halt.c
        hash.c
        mutex.c
        cond.c
        waitgroup.c
        thread.c
        sync.c
        stdio.c
        stdio_uart.c
        puts.c
        random.c
        timestamp.c
        sleep.c
        timer.c
        date.c
        ../date/date.c
        ../mem/arena.c
        ../mem/mem.c
        ../event/init.c
        ../event/lock.c
        ../event/pushpop.c
        ../event/size.c
        ../event/runloop.c
        ../stub/signal.c
        ../stub/arg.c
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
        ../string/float.c
        ../string/has.c
        ../string/scanner.c
        ../string/iostream.c
        ../iostream/iostream.c
        ../hash/djb2.c
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

target_include_directories(picofuse-sys PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    # hash.c pulls in mbedtls headers, which locate mbedtls_config.h via a
    # bare filename #include - see the matching comment in the top-level
    # CMakeLists.txt.
    ${CMAKE_SOURCE_DIR}/include/runtime
)
