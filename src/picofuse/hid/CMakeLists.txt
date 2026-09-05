picofuse_library(
    NAME picofuse-hid
    SOURCES
        adc.c
        event.c
        gpio.c
        hid.c
        iostream.c
        keycode.c
        signal.c
        timer.c
        usb.c
        userbutton.c
        wifi.c
)

target_include_directories(picofuse-hid PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hid PRIVATE picofuse-hw)
