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
        userbutton.c
        wifi.c
        # hw_usb_register_hid() lives with (and is documented alongside)
        # the rest of hw/usb's own API in include/picofuse/hw/usb.h - see
        # that header's own doc - but has to be *compiled* here, not into
        # picofuse-hw: it needs hid_register() from this library, and
        # picofuse-hw sits below picofuse-hid in the link order (the
        # reverse of net/ntp/hid.c's own situation, where net already
        # sits above hid), so it can't link back against it. Matches
        # ../led/led.c's own precedent of a source file living in one
        # module's own directory while being compiled into a different
        # target.
        ../hw/usb/hid.c
)

target_include_directories(picofuse-hid PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hid PRIVATE picofuse-hw)
