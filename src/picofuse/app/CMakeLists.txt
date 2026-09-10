picofuse_library(
    NAME picofuse-app
    SOURCES
        app.c
)

target_include_directories(picofuse-app PRIVATE ${CMAKE_SOURCE_DIR}/include)

# picofuse-app always needs sys/hw/hid/pix - app_main() calls hw_init()/
# hid_init()/pix_poll() etc. directly (see app.c), no weak-symbol fallback.
target_link_libraries(picofuse-app PRIVATE picofuse-sys picofuse-hw picofuse-hid picofuse-pix)
