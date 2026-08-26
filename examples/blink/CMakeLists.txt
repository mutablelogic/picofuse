picofuse_executable(
    NAME blink
    LIBRARIES picosdk picofuse_sys_pico
    HEADERS pico_stdlib_headers
    SOURCES blink.c
)
