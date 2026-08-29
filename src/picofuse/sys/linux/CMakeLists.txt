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

find_package(Threads REQUIRED)
target_link_libraries(picofuse-sys PRIVATE Threads::Threads)
