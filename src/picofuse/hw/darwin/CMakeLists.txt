if(PICOFUSE_WIFI)
    set(_wifi_src wifi.m)
else()
    set(_wifi_src ../stub/wifi.c)
endif()

picofuse_library(
    NAME picofuse-hw
    SOURCES
        init.c
        ../deviceio/deviceio.c
        ../led/led.c
        ../led/blink.c
        ../led/gpio.c
        ../led/pwm.c
        ../led/wifi.c
        ../led/default.c
        ../led/neopixel.c
        ../led/device.c
        ../stub/adc.c
        ../stub/gpio.c
        ../stub/i2c.c
        ../stub/pwm.c
        ../stub/spi.c
        ../stub/watchdog.c
        ../posix/uart.c
        ../posix/address.c
        ${_wifi_src}
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

if(PICOFUSE_WIFI)
    target_link_libraries(picofuse-hw PRIVATE
        "-framework CoreWLAN" "-framework Foundation")
    set_source_files_properties(wifi.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(picofuse-hw PRIVATE PICOFUSE_WIFI=1)
endif()
