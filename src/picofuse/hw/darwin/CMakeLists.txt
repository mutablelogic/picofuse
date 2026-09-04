picofuse_library(
    NAME picofuse-hw
    SOURCES
        init.c
        ../deviceio/deviceio.c
        ../led/led.c
        ../led/led_gpio.c
        ../led/led_pwm.c
        ../led/led_wifi.c
        ../led/led_default.c
        ../led/led_neopixel.c
        ../led/led_device.c
        ../stub/adc.c
        ../stub/gpio.c
        ../stub/i2c.c
        ../stub/pwm.c
        ../stub/spi.c
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)
