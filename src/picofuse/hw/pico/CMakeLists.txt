picofuse_library(
    NAME picofuse-hw
    LIBRARIES picosdk
    HEADERS pico_stdlib_headers
    SOURCES
        adc.c
        dma.c
        gpio.c
        i2c.c
        init.c
        pwm.c
        spi.c
        ../deviceio/deviceio.c
        ../led/stub.c
)

target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

target_include_directories(picofuse-hw PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
