# picofuse_executable(NAME <target> SOURCES <file> ...)
#
# For use OUTSIDE this repository, by a project that only has an installed
# picofuse package (this file's own install location, plus lib/, include/,
# lib/pkgconfig/*.pc alongside it) — not this repository, not the pico-sdk
# source tree. Compiles SOURCES into <target>.elf against picofuse_sys_pico
# — whose own public API is only picofuse/sys.h, on purpose: its .pc has no
# pkg-config Requires: on picosdk at all (pkgconf merges a private
# dependency's Cflags into a consumer's normal --cflags regardless of
# Requires vs Requires.private, so raw SDK headers like hardware/gpio.h
# would leak in through picofuse_sys_pico itself if it declared one), so
# picosdk is found and linked here explicitly instead — with its own
# INTERFACE_INCLUDE_DIRECTORIES stripped straight after, so its symbols/
# wrap-flags/linker-script reach the link step without its headers
# reaching your source files via this macro either.
#
# PKG_CONFIG_PATH must include this installation's lib/pkgconfig before
# calling this.
function(picofuse_executable)
    cmake_parse_arguments(_ARG "" "NAME" "SOURCES" ${ARGN})

    if(NOT _ARG_NAME)
        message(FATAL_ERROR "picofuse_executable: NAME is required")
    endif()
    if(NOT _ARG_SOURCES)
        message(FATAL_ERROR "picofuse_executable: SOURCES is required")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PICOFUSE_SYS_PICO REQUIRED IMPORTED_TARGET picofuse_sys_pico)
    pkg_check_modules(PICOSDK REQUIRED IMPORTED_TARGET picosdk)
    set_target_properties(PkgConfig::PICOSDK PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")

    add_executable(${_ARG_NAME} ${_ARG_SOURCES})
    set_target_properties(${_ARG_NAME} PROPERTIES SUFFIX ".elf")
    target_link_libraries(${_ARG_NAME} PRIVATE PkgConfig::PICOFUSE_SYS_PICO PkgConfig::PICOSDK)
endfunction()
