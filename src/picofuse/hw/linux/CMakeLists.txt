picofuse_library(
    NAME picofuse-hw
    SOURCES
        gpio.c
        i2c.c
        init.c
        pwm.c
        spi.c
        ../deviceio/deviceio.c
        ../gpio/gpio.c
        ../led/led.c
        ../led/blink.c
        ../led/led_gpio.c
        ../led/led_pwm.c
        ../led/led_wifi.c
        ../led/led_default.c
        ../led/led_neopixel.c
        led_device.c
        ../stub/adc.c
        ../posix/uart.c
        # @todo No real Linux hw_wifi_* backend yet (wpa_supplicant control
        # socket - see hw_wifi_init_device()'s doc) to gate behind
        # PICOFUSE_WIFI, unlike darwin/CMakeLists.txt's wifi.m. Always the
        # stub for now. When it's written, add ../posix/address.c (see
        # its own private.h) to implement hw_wifi_get_address() the same
        # way darwin/wifi.m does.
        ../stub/wifi.c
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)
