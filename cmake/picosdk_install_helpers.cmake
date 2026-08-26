# picofuse_write_pkgconfig(...) — called via install(CODE) at `cmake --install`
# time (not configure time), since it needs real string processing (CMake's
# LINKER:X -> -Wl,X translation, in-tree path rewriting) that generator
# expressions can't do. The values it reads were captured earlier at
# *generate* time via file(GENERATE) — the only point CMake can resolve a
# target's fully-transitive INCLUDE_DIRECTORIES/COMPILE_DEFINITIONS/LINK_OPTIONS.
function(picofuse_write_pkgconfig)
    cmake_parse_arguments(_ARG "" "NAME;VERSION;DESCRIPTION;LIBRARY_FILE;INCLUDES_FILE;DEFINES_FILE;LINKOPTS_FILE;SDK_SRC_DIR;BUILD_SDK_DIR;PROJECT_INCLUDE_DIR;PREFIX;CPU_FLAGS;REQUIRES;REQUIRES_PRIVATE;REQUIRES_CAPTURE_DIRS;PUBLIC_HEADER_DIRS" "" ${ARGN})

    # file(READ) + strip, not file(STRINGS): the captured content is already
    # a raw semicolon-separated CMake list (that's exactly what a list *is*
    # internally), so no per-line splitting or escaping is needed — just
    # trim the trailing newline file(GENERATE) adds.
    file(READ "${_ARG_INCLUDES_FILE}" _includes)
    file(READ "${_ARG_DEFINES_FILE}" _defines)
    file(READ "${_ARG_LINKOPTS_FILE}" _linkopts)
    string(STRIP "${_includes}" _includes)
    string(STRIP "${_defines}" _defines)
    string(STRIP "${_linkopts}" _linkopts)

    # A required package's own .pc already carries whatever it captured
    # (pkg-config's Requires: merges it in for any consumer) — so subtract
    # it here rather than baking the same -I/-D/-Wl flags into this
    # package's Cflags/Libs too. picofuse_sys_pico's own transitive
    # properties include everything picosdk's do (it privately links
    # picosdk_static to build), so without this every downstream package
    # would re-duplicate the entire upstream one's flags verbatim.
    if(_ARG_REQUIRES_CAPTURE_DIRS)
        separate_arguments(_req_dirs UNIX_COMMAND "${_ARG_REQUIRES_CAPTURE_DIRS}")
        foreach(_req_dir IN LISTS _req_dirs)
            foreach(_prop includes defines linkopts)
                set(_req_file "${_req_dir}/${_prop}.txt")
                if(EXISTS "${_req_file}")
                    file(READ "${_req_file}" _req_val)
                    string(STRIP "${_req_val}" _req_val)
                    if(_req_val)
                        list(REMOVE_ITEM _${_prop} ${_req_val})
                    endif()
                endif()
            endforeach()
        endforeach()
    endif()

    # PUBLIC_HEADER_DIRS marks this as a *scoped* package (e.g.
    # picofuse_sys_pico): its own public API is just those specific
    # directories, not the whole SDK header surface — so, unlike the
    # unscoped case below, it gets its own dedicated include root instead
    # of dumping into (and pointing -I at) the shared includedir tree.
    # Without that separation, any package whose own INCLUDE_DIRECTORIES
    # happens to include the shared tree (true here, since
    # picofuse_sys_pico privately links picosdk_static to build) would let
    # a consumer reach raw SDK headers like hardware/gpio.h through it,
    # even though that's not this package's actual API.
    if(_ARG_PUBLIC_HEADER_DIRS)
        separate_arguments(_public_header_dirs UNIX_COMMAND "${_ARG_PUBLIC_HEADER_DIRS}")
        foreach(_hdr_dir IN LISTS _public_header_dirs)
            if(IS_DIRECTORY "${_ARG_PROJECT_INCLUDE_DIR}/${_hdr_dir}")
                file(COPY "${_ARG_PROJECT_INCLUDE_DIR}/${_hdr_dir}"
                    DESTINATION "${_ARG_PREFIX}/lib/${_ARG_NAME}/include")
            endif()
        endforeach()
        set(_cflags "-I\${libdir}/${_ARG_NAME}/include")
    else()
        # Every SDK module's own include/ dir already uses the final
        # relative path (e.g. hardware/gpio.h), so copying each one's
        # *contents* into the same destination merges them into a single
        # consolidated tree with no restructuring — and the .pc file then
        # only ever needs one -I. The project's own include/ dir is
        # skipped here: an unscoped package (picosdk) has no business
        # exposing picofuse/ as if it were its own API either.
        foreach(_dir IN LISTS _includes)
            if(IS_DIRECTORY "${_dir}" AND NOT _dir STREQUAL "${_ARG_PROJECT_INCLUDE_DIR}")
                file(COPY "${_dir}/" DESTINATION "${_ARG_PREFIX}/include")
            endif()
        endforeach()
        set(_cflags "-I\${includedir}")
    endif()
    foreach(_define IN LISTS _defines)
        if(_define)
            # pkg-config runs Cflags/Libs through its own shell-like
            # tokenizer; a bare " is shell-quoting syntax to it (stripped),
            # not a literal character, so defines like PICO_BOARD="pico"
            # need it backslash-escaped to survive as a real C string literal.
            string(REPLACE "\"" "\\\"" _define_escaped "${_define}")
            string(APPEND _cflags " -D${_define_escaped}")
        endif()
    endforeach()
    if(_ARG_CPU_FLAGS)
        string(APPEND _cflags " ${_ARG_CPU_FLAGS}")
    endif()

    set(_libs "-L\${libdir} -l${_ARG_NAME}")
    set(_script_dirs "")
    foreach(_opt IN LISTS _linkopts)
        if(NOT _opt)
            continue()
        endif()
        # CMake's own LINKER:a,b -> -Wl,a,b syntax; anything not prefixed
        # this way (-nostartfiles, --gc-sections, ...) passes through as-is.
        if(_opt MATCHES "^LINKER:(.*)$")
            set(_opt "-Wl,${CMAKE_MATCH_1}")
            if(_opt STREQUAL "-Wl,")
                continue()
            endif()
        endif()

        # These flags name the exact directories the linker script system
        # needs (search dirs and the scripts themselves) — collecting them
        # here means copying only those below, rather than sweeping the
        # whole SDK src/ tree and recreating every directory in it, matched
        # files or not (file(COPY ... FILES_MATCHING) does the latter).
        if(_opt MATCHES "^-Wl,-L(.+)$")
            list(APPEND _script_dirs "${CMAKE_MATCH_1}")
        elseif(_opt MATCHES "^-Wl,--script=(.+)$" OR _opt MATCHES "^-T(.+)$")
            get_filename_component(_script_dir "${CMAKE_MATCH_1}" DIRECTORY)
            list(APPEND _script_dirs "${_script_dir}")
        endif()

        # Point at the copy installed below instead of the original
        # (likely absent on a third party's machine) in-tree/build-tree path.
        string(REPLACE "${_ARG_SDK_SRC_DIR}" "\${libdir}/picosdk/src" _opt "${_opt}")
        string(REPLACE "${_ARG_BUILD_SDK_DIR}" "\${libdir}/picosdk/src" _opt "${_opt}")
        string(APPEND _libs " ${_opt}")
    endforeach()
    if(_ARG_CPU_FLAGS)
        string(APPEND _libs " ${_ARG_CPU_FLAGS}")
    endif()

    if(_script_dirs)
        list(REMOVE_DUPLICATES _script_dirs)
    endif()
    foreach(_dir IN LISTS _script_dirs)
        string(FIND "${_dir}" "${_ARG_SDK_SRC_DIR}/" _pos)
        if(_pos EQUAL 0)
            string(REPLACE "${_ARG_SDK_SRC_DIR}/" "" _rel "${_dir}")
            file(COPY "${_dir}/" DESTINATION "${_ARG_PREFIX}/lib/picosdk/src/${_rel}"
                FILES_MATCHING PATTERN "*.ld" PATTERN "*.incl")
            continue()
        endif()
        string(FIND "${_dir}" "${_ARG_BUILD_SDK_DIR}/" _pos)
        if(_pos EQUAL 0)
            string(REPLACE "${_ARG_BUILD_SDK_DIR}/" "" _rel "${_dir}")
            file(COPY "${_dir}/" DESTINATION "${_ARG_PREFIX}/lib/picosdk/src/${_rel}"
                FILES_MATCHING PATTERN "*.ld" PATTERN "*.incl")
        endif()
    endforeach()

    set(_requires_line "")
    if(_ARG_REQUIRES)
        string(APPEND _requires_line "Requires: ${_ARG_REQUIRES}\n")
    endif()
    if(_ARG_REQUIRES_PRIVATE)
        string(APPEND _requires_line "Requires.private: ${_ARG_REQUIRES_PRIVATE}\n")
    endif()

    file(WRITE "${_ARG_PREFIX}/lib/pkgconfig/${_ARG_NAME}.pc"
"prefix=${_ARG_PREFIX}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: ${_ARG_NAME}
Description: ${_ARG_DESCRIPTION}
Version: ${_ARG_VERSION}
${_requires_line}Cflags: ${_cflags}
Libs: ${_libs}
")
endfunction()
