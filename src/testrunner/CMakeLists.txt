# Runs on the host to flash/run a .elf on real Pico hardware via openocd -
# not something that itself gets cross-compiled for a Pico target.
if(NOT DEFINED PICO_BOARD)
    picofuse_executable(
        NAME testrunner
        LIBRARIES picofuse-sys
        SOURCES main.c exec.c serial.c
    )
    target_include_directories(testrunner PRIVATE ${CMAKE_SOURCE_DIR}/include)

    # It doesn't use everything picofuse-sys links to (OpenSSL, notably), so
    # don't depend on those being present where it's installed.
    if(APPLE)
        target_link_options(testrunner PRIVATE "LINKER:-dead_strip_dylibs")
    else()
        target_link_options(testrunner PRIVATE "LINKER:--as-needed")
    endif()

    # Not install(TARGETS), which would install testrunner-<version> and a link
    # to it, leaving the old one behind each time the version changes.
    install(PROGRAMS $<TARGET_FILE:testrunner> DESTINATION bin RENAME testrunner)
    install(FILES README.md DESTINATION docs RENAME testrunner.md)
endif()
