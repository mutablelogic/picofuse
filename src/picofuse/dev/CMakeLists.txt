
# SDL support - host-only. pkg-config always searches the host's own
# library paths regardless of cross-compilation, so on a Pico build this
# would otherwise "find" and try to link against the host's native SDL2
# (wrong ABI/arch entirely) - and SDL2 makes no sense on Pico anyway, with
# no windowing system to speak of.
if(NOT DEFINED PICO_BOARD)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()
endif()
if(SDL2_FOUND)
    message(STATUS "Found SDL2: enabling SDL display backend")
    set(_sdl_src sdl/sdl.c)
else()
    message(STATUS "SDL2 not found: SDL display backend disabled")
    set(_sdl_src sdl/stub.c)
endif()

picofuse_library(
    NAME picofuse-dev
    SOURCES
        bme680/bme680.c
        ft6236/ft6236.c
        ft6236/hid.c
        ili9341/ili9341.c
        stmpe610/stmpe610.c
        stmpe610/hid.c
        ${_sdl_src}
        st7701/st7701.c
)

target_include_directories(picofuse-dev PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-dev PRIVATE picofuse-hw picofuse-hid picofuse-pix)
if(SDL2_FOUND)
    target_link_libraries(picofuse-dev PRIVATE PkgConfig::SDL2)
endif()
