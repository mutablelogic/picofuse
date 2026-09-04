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
        ../led/led.c
        ../led/led_gpio.c
        ../led/led_pwm.c
        led_wifi.c
        led_default.c
        led_neopixel.c
)

pico_generate_pio_header(picofuse-hw ${CMAKE_CURRENT_LIST_DIR}/led_neopixel.pio)

target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

target_include_directories(picofuse-hw PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
