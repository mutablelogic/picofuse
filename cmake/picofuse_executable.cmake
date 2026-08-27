# picofuse_executable(NAME <target> SOURCES <file> ...)
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
