# picofuse_executable(NAME <target> SOURCES <file> ...)
#
# For use OUTSIDE this repository, by a project that only has an installed
# picofuse package (this file's own install location, plus lib/, include/,
# lib/pkgconfig/*.pc alongside it) — not this repository, not the pico-sdk
# source tree. Finds picofuse_sys_pico via pkg-config (which pulls in
# picosdk transitively through its own Requires: picosdk) and compiles
# SOURCES into <target>.elf against it. PKG_CONFIG_PATH must include this
# installation's lib/pkgconfig before calling this.
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

    add_executable(${_ARG_NAME} ${_ARG_SOURCES})
    set_target_properties(${_ARG_NAME} PROPERTIES SUFFIX ".elf")
    target_link_libraries(${_ARG_NAME} PRIVATE PkgConfig::PICOFUSE_SYS_PICO)
endfunction()
