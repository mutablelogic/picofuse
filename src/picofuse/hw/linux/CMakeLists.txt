# libusb-1.0 has no CMake config package - discovered via pkg-config, same
# as picofuse-sys's own hw/linux/CMakeLists.txt did.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_search_module(LIBUSB QUIET libusb-1.0)
endif()

set(_usb_src ../stub/usb.c)
if(PICOFUSE_USB AND LIBUSB_FOUND)
    message(STATUS "Found libusb-1.0: enabling Linux USB backend")
    set(_usb_src ../libusb/usb.c)
elseif(PICOFUSE_USB)
    message(STATUS "libusb-1.0 not found: using Linux USB stub backend")
endif()

picofuse_library(
    NAME picofuse-hw
    SOURCES
        gpio.c
        i2c.c
        init.c
        ../memory/memory.c
        pwm.c
        spi.c
        ../deviceio/deviceio.c
        ../block/block.c
        ../gpio/gpio.c
        ../led/led.c
        ../usb/usb.c
        ../led/blink.c
        ../led/gpio.c
        ../led/pwm.c
        ../led/wifi.c
        ../led/default.c
        ../led/neopixel.c
        led_device.c
        ../stub/adc.c
        ../stub/flash.c
        ../posix/uart.c
        watchdog.c
        # @todo No real Linux hw_wifi_* backend yet (wpa_supplicant control
        # socket - see hw_wifi_init_device()'s doc) to gate behind
        # PICOFUSE_WIFI, unlike darwin/CMakeLists.txt's wifi.m. Always the
        # stub for now. When it's written, add ../posix/address.c (see
        # its own private.h) to implement hw_wifi_get_address() the same
        # way darwin/wifi.m does.
        ../stub/wifi.c
        ${_usb_src}
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

if(PICOFUSE_USB AND LIBUSB_FOUND)
    find_package(Threads QUIET)

    target_include_directories(picofuse-hw PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_compile_options(picofuse-hw PRIVATE ${LIBUSB_CFLAGS_OTHER})
    target_link_directories(picofuse-hw PUBLIC ${LIBUSB_LIBRARY_DIRS})
    target_link_libraries(picofuse-hw PUBLIC ${LIBUSB_LIBRARIES})
    target_link_options(picofuse-hw PUBLIC ${LIBUSB_LDFLAGS_OTHER})

    if(Threads_FOUND)
        target_link_libraries(picofuse-hw PUBLIC Threads::Threads)
    endif()
endif()
