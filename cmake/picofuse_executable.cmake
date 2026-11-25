# The prefix this file is installed under, and so the one to build against.
get_filename_component(_picofuse_prefix "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

# What to build for: PICO_BOARD if it's set, or else the host. Libraries are
# installed in lib/${PICOFUSE_TARGET}.
if(PICO_BOARD)
    set(PICOFUSE_TARGET "${PICO_BOARD}")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" PICOFUSE_TARGET)
endif()
set(_picofuse_lib "${_picofuse_prefix}/lib/${PICOFUSE_TARGET}")
if(NOT IS_DIRECTORY "${_picofuse_lib}/pkgconfig")
    file(GLOB _picofuse_targets RELATIVE "${_picofuse_prefix}/lib" "${_picofuse_prefix}/lib/*/pkgconfig")
    string(REPLACE "/pkgconfig" "" _picofuse_targets "${_picofuse_targets}")
    list(JOIN _picofuse_targets ", " _picofuse_targets)
    message(FATAL_ERROR "picofuse isn't installed for ${PICOFUSE_TARGET} in ${_picofuse_prefix}, only for: ${_picofuse_targets}")
endif()

# This target first, whatever else PKG_CONFIG_PATH already names.
set(ENV{PKG_CONFIG_PATH} "${_picofuse_lib}/pkgconfig:$ENV{PKG_CONFIG_PATH}")

# picofuse_executable(NAME <target>
#                      LIBRARIES <lib> ...
#                      [VERSION <version>]
#                      SOURCES <file> ...)
#
# Builds SOURCES into <target> (<target>.elf for a Pico board), linked
# against the installed picofuse LIBRARIES (e.g. picofuse-sys, picofuse-app)
# and whatever those need. NAME and VERSION are what sys_env_name() and
# sys_env_version() return on a Pico.
function(picofuse_executable)
    cmake_parse_arguments(_ARG "" "NAME;VERSION" "LIBRARIES;SOURCES" ${ARGN})

    if(NOT _ARG_NAME)
        message(FATAL_ERROR "picofuse_executable: NAME is required")
    endif()
    if(NOT _ARG_SOURCES)
        message(FATAL_ERROR "picofuse_executable: SOURCES is required")
    endif()
    if(NOT _ARG_LIBRARIES)
        set(_ARG_LIBRARIES picofuse-sys)
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PICOFUSE_${_ARG_NAME} REQUIRED IMPORTED_TARGET ${_ARG_LIBRARIES})

    add_executable(${_ARG_NAME} ${_ARG_SOURCES})
    target_link_libraries(${_ARG_NAME} PRIVATE PkgConfig::PICOFUSE_${_ARG_NAME})

    if(EXISTS "${_picofuse_lib}/pkgconfig/picosdk.pc")
        set_target_properties(${_ARG_NAME} PROPERTIES SUFFIX ".elf")

        # A separate object library, so the SDK headers this needs aren't
        # visible to SOURCES.
        pkg_check_modules(PICOSDK_HEADERS REQUIRED IMPORTED_TARGET picosdk-headers)
        set(_src "#include <pico/binary_info.h>\nbi_decl(bi_program_name(\"${_ARG_NAME}\"))\n")
        if(_ARG_VERSION)
            string(APPEND _src "bi_decl(bi_program_version_string(\"${_ARG_VERSION}\"))\n")
        endif()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${_ARG_NAME}_bi.c" "${_src}")
        add_library(${_ARG_NAME}_bi OBJECT "${CMAKE_CURRENT_BINARY_DIR}/${_ARG_NAME}_bi.c")
        target_compile_options(${_ARG_NAME}_bi PRIVATE ${PICOFUSE_${_ARG_NAME}_CFLAGS_OTHER})
        target_link_libraries(${_ARG_NAME}_bi PRIVATE PkgConfig::PICOSDK_HEADERS)
        target_sources(${_ARG_NAME} PRIVATE $<TARGET_OBJECTS:${_ARG_NAME}_bi>)
    endif()
endfunction()
