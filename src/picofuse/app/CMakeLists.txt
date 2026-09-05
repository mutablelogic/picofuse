picofuse_library(
    NAME picofuse-app
    SOURCES
        app.c
        hid.c
        hw.c
)

target_include_directories(picofuse-app PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-app PRIVATE picofuse-sys)

# Deliberately not a hard dependency on picofuse-hw/picofuse-hid: hw.c and
# hid.c provide weak fallback symbols so picofuse-app works standalone (see
# their own file comments). Consumers that want hw/hid integration opt in
# by also linking picofuse-hw/picofuse-hid alongside picofuse-app, listing
# picofuse-app FIRST so the real symbols (scanned later) override the weak
# ones.
