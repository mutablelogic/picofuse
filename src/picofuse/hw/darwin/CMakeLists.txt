if(PICOFUSE_WIFI)
    set(_wifi_src wifi.m)
else()
    set(_wifi_src ../stub/wifi.c)
endif()

# libusb-1.0 has no CMake config package - discovered via pkg-config, same
# as picofuse-sys's own hw/darwin/CMakeLists.txt did.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_search_module(LIBUSB QUIET libusb-1.0)
endif()

set(_usb_src ../stub/usb.c)
if(PICOFUSE_USB AND LIBUSB_FOUND)
    message(STATUS "Found libusb-1.0: enabling Darwin USB backend")
    set(_usb_src ../libusb/usb.c)
elseif(PICOFUSE_USB)
    message(STATUS "libusb-1.0 not found: using Darwin USB stub backend")
endif()

picofuse_library(
    NAME picofuse-hw
    SOURCES
        init.c
        ../memory/memory.c
        ../block/block.c
        ../deviceio/deviceio.c
        ../led/led.c
        ../usb/usb.c
        ../led/blink.c
        ../led/gpio.c
        ../led/pwm.c
        ../led/wifi.c
        ../led/default.c
        ../led/neopixel.c
        ../led/device.c
        ../stub/adc.c
        ../stub/flash.c
        ../stub/gpio.c
        ../stub/i2c.c
        ../stub/pwm.c
        ../stub/spi.c
        ../stub/watchdog.c
        ../posix/uart.c
        ../posix/address.c
        ${_wifi_src}
        ${_usb_src}
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)

if(PICOFUSE_WIFI)
    target_link_libraries(picofuse-hw PRIVATE
        "-framework CoreWLAN" "-framework Foundation")
    set_source_files_properties(wifi.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(picofuse-hw PRIVATE PICOFUSE_WIFI=1)
endif()

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
