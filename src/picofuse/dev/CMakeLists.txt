picofuse_library(
    NAME picofuse-dev
    SOURCES
        bme680/bme680.c
        ft6236/ft6236.c
        # dev_ft6236_register_hid() - declared in and documented alongside
        # the rest of dev/ft6236.h, needs hid_register()/
        # hid_event_queue_touch() from picofuse-hid. hid itself must not
        # know about specific device drivers, so the dependency runs this
        # direction only: picofuse-dev links picofuse-hid below, not the
        # other way around (unlike hw_usb_register_hid(), which has to be
        # compiled into picofuse-hid instead, since picofuse-hw sits below
        # picofuse-hid and can't link back up to it - see
        # src/picofuse/hid/CMakeLists.txt's own comment).
        ft6236/hid.c
        ili9341/ili9341.c
        stmpe610/stmpe610.c
        # dev_stmpe610_register_hid() - same reasoning as ft6236/hid.c
        # above.
        stmpe610/hid.c
)

target_include_directories(picofuse-dev PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-dev PRIVATE picofuse-hw picofuse-hid)
