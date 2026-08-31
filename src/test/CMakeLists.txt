# Runs on the host to flash/run a .elf on real Pico hardware via openocd -
# not something that itself gets cross-compiled for a Pico target.
if(NOT DEFINED PICO_BOARD)
    picofuse_executable(
        NAME testrunner
        LIBRARIES picofuse-sys
        SOURCES main.c exec.c
    )
    target_include_directories(testrunner PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()
