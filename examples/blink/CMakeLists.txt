picofuse_executable(
    NAME blink
    LIBRARIES picosdk picofuse-sys
    HEADERS pico_stdlib_headers
    SOURCES blink.c
)
