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
        ../led/led_device.c
)

pico_generate_pio_header(picofuse-hw ${CMAKE_CURRENT_LIST_DIR}/led_neopixel.pio)

target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

target_include_directories(picofuse-hw PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

if(PICO_CYW43_SUPPORTED)
    # init.c pulls in pico/cyw43_arch.h (transitively lwip/opt.h), which
    # needs lwipopts.h findable on the include path - matches the same
    # requirement picosdk_static already has for its own cyw43 usage.
    target_include_directories(picofuse-hw PRIVATE
        ${CMAKE_SOURCE_DIR}/include/runtime
    )
endif()
