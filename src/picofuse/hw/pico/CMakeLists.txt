# wifi.c needs real CYW43 hardware - a board without it (no
# PICO_CYW43_SUPPORTED) always falls back to the stub regardless of
# PICOFUSE_WIFI.
if(PICOFUSE_WIFI AND PICO_CYW43_SUPPORTED)
    set(_wifi_src wifi.c)
else()
    set(_wifi_src ../stub/wifi.c)
endif()

# usb.c needs the TinyUSB host stack (tinyusb_host, added to _picosdk_targets
# in the top-level CMakeLists.txt) - unlike wifi.c's PICO_CYW43_SUPPORTED,
# every RP2040/RP2350 board has the same on-chip USB controller, so this is
# gated on PICOFUSE_USB alone.
if(PICOFUSE_USB)
    set(_usb_src usb.c)
    add_compile_definitions(PICOFUSE_USB=1)
else()
    set(_usb_src ../stub/usb.c)
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
        watchdog.c
        ../deviceio/deviceio.c
        ../led/led.c
        ../usb/usb.c
        ../led/blink.c
        ../led/gpio.c
        ../led/pwm.c
        led_wifi.c
        led_default.c
        led_neopixel.c
        ../led/device.c
        ${_wifi_src}
        ${_usb_src}
)

pico_generate_pio_header(picofuse-hw ${CMAKE_CURRENT_LIST_DIR}/led_neopixel.pio)

target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

target_include_directories(picofuse-hw PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

if(PICO_CYW43_SUPPORTED OR PICOFUSE_USB)
    # init.c pulls in pico/cyw43_arch.h (transitively lwip/opt.h), which
    # needs lwipopts.h findable on the include path - matches the same
    # requirement picosdk_static already has for its own cyw43 usage. usb.c
    # pulls in tusb.h the same way, needing tusb_config.h on the path.
    target_include_directories(picofuse-hw PRIVATE
        ${CMAKE_SOURCE_DIR}/include/runtime/pico
    )
endif()
