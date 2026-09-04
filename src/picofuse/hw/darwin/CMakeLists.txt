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
        ${_wifi_src}
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

if(PICOFUSE_WIFI)
    target_link_libraries(picofuse-hw PRIVATE
        "-framework CoreWLAN" "-framework Foundation")

    # wifi.m is the only Objective-C file in the project - ARC keeps its
    # CoreWLAN/Foundation object lifetimes (CWInterface, NSString, ...)
    # correct without manual retain/release.
    set_source_files_properties(wifi.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
