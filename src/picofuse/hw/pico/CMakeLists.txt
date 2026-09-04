# wifi.c needs real CYW43 hardware - a board without it (no
# PICO_CYW43_SUPPORTED) always falls back to the stub regardless of
# PICOFUSE_WIFI.
if(PICOFUSE_WIFI AND PICO_CYW43_SUPPORTED)
    set(_wifi_src wifi.c)
else()
    set(_wifi_src ../stub/wifi.c)
endif()

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
        uart.c
        # @todo No real Pico hw_infrared_* backend yet (ir_tx.pio is
        # written but not wired into a driver, and RX has no PIO backend
        # here) - always the stub for now.
        ../stub/infrared.c
        ../deviceio/deviceio.c
        ../led/led.c
        ../led/led_gpio.c
        ../led/led_pwm.c
        led_wifi.c
        led_default.c
        led_neopixel.c
        ../led/led_device.c
        ${_wifi_src}
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
        ${CMAKE_SOURCE_DIR}/include/runtime/pico
    )
endif()
