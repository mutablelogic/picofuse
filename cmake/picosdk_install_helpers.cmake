# picofuse_write_picosdk(...) - called via install(CODE) at `cmake --install`
# time (not configure time), since it needs real string processing that
# generator expressions can't do. The values it reads were captured at
# *generate* time - see picofuse_install_picosdk().
function(picofuse_write_picosdk)
    cmake_parse_arguments(_ARG "" "VERSION;BOARD;PLATFORM;CPU_FLAGS;CAPTURE_DIR;BOOT2_FILE;SDK_DIR;BUILD_SDK_DIR;PROJECT_INCLUDE_DIR;PREFIX" "" ${ARGN})

    # file(READ) + strip, not file(STRINGS): the captured content is already a
    # raw semicolon-separated CMake list.
    foreach(_prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_OPTIONS)
        file(READ "${_ARG_CAPTURE_DIR}/${_prop}.txt" _val)
        string(STRIP "${_val}" _val)
        list(REMOVE_ITEM _val "")
        list(REMOVE_DUPLICATES _val)
        set(_${_prop} "${_val}")
    endforeach()

    set(_lib "${_ARG_PREFIX}/lib/${_ARG_BOARD}")
    set(_root "${_lib}/picosdk")

    ###########################################################################
    # Headers: every SDK module's own include/ dir already uses the final
    # relative path (e.g. hardware/gpio.h), so copying the contents of each
    # into one place merges them into a single tree needing a single -I.
    # Globbed rather than file(COPY ... FILES_MATCHING), which recreates every
    # directory it visits whether or not anything in it matched. The project's
    # own include/ is skipped: it isn't part of the SDK.
    foreach(_dir IN LISTS _INCLUDE_DIRECTORIES)
        if(NOT IS_DIRECTORY "${_dir}" OR _dir STREQUAL _ARG_PROJECT_INCLUDE_DIR)
            continue()
        endif()
        file(GLOB_RECURSE _headers RELATIVE "${_dir}"
            "${_dir}/*.h" "${_dir}/*.inc" "${_dir}/*.S")
        foreach(_header IN LISTS _headers)
            get_filename_component(_header_dir "${_header}" DIRECTORY)
            file(COPY "${_dir}/${_header}" DESTINATION "${_root}/include/${_header_dir}")
        endforeach()
    endforeach()

    # config_autogen.h includes the board header, and others, by absolute
    # path. Rewrite each relative to whichever include dir it came from, also
    # copying any that only exist in the project's own include/ (i.e. the
    # board headers in include/boards).
    set(_autogen "${_root}/include/pico/config_autogen.h")
    if(EXISTS "${_autogen}")
        file(READ "${_autogen}" _content)
        string(REGEX MATCHALL "#include \"/[^\"]+\"" _absolutes "${_content}")
        foreach(_absolute IN LISTS _absolutes)
            string(REGEX REPLACE "^#include \"(.*)\"$" "\\1" _file "${_absolute}")
            set(_rel "")
            foreach(_dir IN LISTS _INCLUDE_DIRECTORIES)
                string(FIND "${_file}" "${_dir}/" _pos)
                if(_pos EQUAL 0)
                    string(REPLACE "${_dir}/" "" _rel "${_file}")
                    break()
                endif()
            endforeach()
            # A module can contribute a config header without its include/ dir
            # being one of ours (cmsis does).
            if(NOT _rel AND _file MATCHES "^.*/include/(.+)$")
                set(_rel "${CMAKE_MATCH_1}")
            endif()
            if(NOT _rel)
                message(FATAL_ERROR "picofuse_write_picosdk: ${_file} isn't under any include directory")
            endif()
            get_filename_component(_rel_dir "${_rel}" DIRECTORY)
            file(COPY "${_file}" DESTINATION "${_root}/include/${_rel_dir}")
            string(REPLACE "${_absolute}" "#include \"${_rel}\"" _content "${_content}")
        endforeach()
        file(WRITE "${_autogen}" "${_content}")
    endif()

    ###########################################################################
    # Link flags. picosdk calls back into picofuse-sys (sys_debugf(), ...), so
    # it has to be followed by it on the link line.
    set(_libs "-L\${libdir}")
    if(_ARG_BOOT2_FILE)
        file(READ "${_ARG_BOOT2_FILE}" _boot2)
        string(STRIP "${_boot2}" _boot2)
        get_filename_component(_boot2_name "${_boot2}" NAME)
        file(COPY "${_boot2}" DESTINATION "${_root}/boot2")
        string(APPEND _libs " \${libdir}/picosdk/boot2/${_boot2_name}")
    endif()

    set(_script_dirs "")
    foreach(_opt IN LISTS _LINK_OPTIONS)
        # CMake's own LINKER:a,b -> -Wl,a,b syntax; anything not prefixed this
        # way (-nostartfiles, ...) passes through as-is.
        if(_opt MATCHES "^LINKER:(.*)$")
            set(_opt "-Wl,${CMAKE_MATCH_1}")
            if(_opt STREQUAL "-Wl,")
                continue()
            endif()
        endif()

        # These name the directories the linker script system needs. Copying
        # just those is enough, and avoids sweeping the whole SDK.
        if(_opt MATCHES "^-Wl,-L(.+)$")
            list(APPEND _script_dirs "${CMAKE_MATCH_1}")
        elseif(_opt MATCHES "^(-Wl,--script=|-T)(.+)$")
            get_filename_component(_script_dir "${CMAKE_MATCH_2}" DIRECTORY)
            list(APPEND _script_dirs "${_script_dir}")
        endif()

        string(REPLACE "${_ARG_SDK_DIR}" "\${libdir}/picosdk/src" _opt "${_opt}")
        string(REPLACE "${_ARG_BUILD_SDK_DIR}" "\${libdir}/picosdk/src" _opt "${_opt}")
        string(APPEND _libs " ${_opt}")
    endforeach()

    list(REMOVE_DUPLICATES _script_dirs)
    foreach(_dir IN LISTS _script_dirs)
        set(_rel "")
        foreach(_base "${_ARG_SDK_DIR}" "${_ARG_BUILD_SDK_DIR}")
            string(FIND "${_dir}" "${_base}/" _pos)
            if(_pos EQUAL 0)
                string(REPLACE "${_base}/" "" _rel "${_dir}")
            endif()
        endforeach()
        if(NOT _rel)
            message(FATAL_ERROR "picofuse_write_picosdk: linker script directory ${_dir} isn't under the SDK or build tree")
        endif()
        file(GLOB_RECURSE _scripts RELATIVE "${_dir}" "${_dir}/*.ld" "${_dir}/*.incl")
        foreach(_script IN LISTS _scripts)
            get_filename_component(_script_rel "${_script}" DIRECTORY)
            file(COPY "${_dir}/${_script}" DESTINATION "${_root}/src/${_rel}/${_script_rel}")
        endforeach()
    endforeach()

    # -Wl,--whole-archive: see picofuse_add_pico_sdk_bundle() for why.
    string(APPEND _libs
        " -Wl,--whole-archive,\${libdir}/libpicosdk.a,--no-whole-archive"
        " -lpicofuse-sys ${_ARG_CPU_FLAGS}")

    ###########################################################################
    # -fshort-enums is arm-none-eabi-gcc's default but not clang's, and picofuse's
    # structs contain enums, so a consumer compiled without it would silently
    # disagree with the libraries about their layout.
    set(_header
"prefix=\${pcfiledir}/../../..
libdir=\${prefix}/lib/${_ARG_BOARD}
board=${_ARG_BOARD}
platform=${_ARG_PLATFORM}
")
    file(WRITE "${_lib}/pkgconfig/picosdk.pc"
"${_header}
Name: picosdk
Description: pico-sdk runtime for ${_ARG_BOARD}
Version: ${_ARG_VERSION}
Cflags: ${_ARG_CPU_FLAGS} -fshort-enums
Libs: ${_libs}
")

    set(_cflags "-I\${libdir}/picosdk/include")
    foreach(_define IN LISTS _COMPILE_DEFINITIONS)
        # pkg-config runs Cflags through its own shell-like tokenizer, so
        # defines like PICO_BOARD="pico" need their quotes escaped to survive
        # as a C string literal.
        string(REPLACE "\"" "\\\"" _define "${_define}")
        string(APPEND _cflags " -D${_define}")
    endforeach()
    file(WRITE "${_lib}/pkgconfig/picosdk-headers.pc"
"${_header}
Name: picosdk-headers
Description: pico-sdk headers for ${_ARG_BOARD}
Version: ${_ARG_VERSION}
Cflags: ${_cflags}
")
endfunction()
