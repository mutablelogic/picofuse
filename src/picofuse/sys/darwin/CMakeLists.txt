find_package(OpenSSL)

if(OPENSSL_FOUND)
    set(_PICOFUSE_RANDOM_SOURCE ../openssl/random.c)
    set(_PICOFUSE_HASH_SOURCE ../openssl/hash.c)
    set_property(GLOBAL PROPERTY PICOFUSE_HASH_BACKEND openssl)
else()
    set(_PICOFUSE_RANDOM_SOURCE ../posix/random.c)
    set(_PICOFUSE_HASH_SOURCE ../stub/hash.c)
    set_property(GLOBAL PROPERTY PICOFUSE_HASH_BACKEND stub)
endif()

picofuse_library(
    NAME picofuse-sys
    SOURCES
        ../posix/halt.c
        ../posix/init.c
        ../posix/puts.c
        ${_PICOFUSE_RANDOM_SOURCE}
        ../posix/sleep.c
        ../posix/timestamp.c
        ../posix/date.c
        ../posix/signal.c
        ../date/date.c
        ../mem/arena.c
        ../env/env.c
        ../env/arg.c
        env.c
        timer.c
        ../pthread/mutex.c
        ../pthread/cond.c
        ../pthread/waitgroup.c
        ../pthread/thread.c
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
        ${_PICOFUSE_HASH_SOURCE}
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

if(OPENSSL_FOUND)
    target_link_libraries(picofuse-sys PRIVATE OpenSSL::Crypto)
endif()

target_link_libraries(picofuse-sys PRIVATE "-framework IOKit" "-framework CoreFoundation")

