# picofuse_executable(NAME <target>
#                      LIBRARIES <lib> ...
#                      [HEADERS <headers-only-target> ...]
#                      SOURCES <file> ...)
#
# On a PICO_BOARD build, compiles SOURCES into <target>.elf, linked against
# pico-sdk libraries already built via picofuse_add_pico_sdk_library() — pass
# their plain SDK names (e.g. pico_stdio, hardware_gpio), not the _static
# target name — and/or libraries built via picofuse_library(), passed by
# their real target name. #include resolution comes from those libraries'
# own SDK headers, plus whatever PICO_BOARD's board header defines (both
# already wired into each library by picofuse_add_pico_sdk_library()).
#
# On a native (darwin/linux) build, compiles SOURCES into a plain <target>
# executable, linked normally against LIBRARIES' real target names.
#
# HEADERS links extra headers-only ("*_headers") SDK targets directly, for
# convenience umbrella headers like pico/stdlib.h (pico_stdlib_headers) that
# aren't backed by a picofuse_add_pico_sdk_library() build of their own.
# As with picofuse_add_pico_sdk_library()'s HEADER_DEPS, only pass "*_headers"
# targets here — a real, source-bearing target would compile its sources a
# second time directly into this executable, on top of the copies already
# sitting in whichever LIBRARIES .a legitimately links it in.
function(picofuse_executable)
    cmake_parse_arguments(_ARG "" "NAME" "LIBRARIES;HEADERS;SOURCES" ${ARGN})

    if(NOT _ARG_NAME)
        message(FATAL_ERROR "picofuse_executable: NAME is required")
    endif()
    if(NOT _ARG_SOURCES)
        message(FATAL_ERROR "picofuse_executable: SOURCES is required")
    endif()

    add_executable(${_ARG_NAME} ${_ARG_SOURCES})

    set(_static_libs)
    foreach(_LIB IN LISTS _ARG_LIBRARIES)
        if(TARGET ${_LIB}_static)
            list(APPEND _static_libs ${_LIB}_static)
        elseif(TARGET ${_LIB})
            list(APPEND _static_libs ${_LIB})
        else()
            message(FATAL_ERROR "picofuse_executable: ${_LIB} was not built via picofuse_add_pico_sdk_library()/picofuse_add_pico_sdk_bundle()/picofuse_library()")
        endif()
    endforeach()

    if(DEFINED PICO_BOARD)
        set_target_properties(${_ARG_NAME} PROPERTIES SUFFIX ".elf")

        # pico-sdk static libs genuinely call into each other in both
        # directions (e.g. hardware_gpio's panic() calls pico_stdio's wrapped
        # puts()), which a single left-to-right archive scan can miss
        # depending on link order. Wrapping them in --start-group/--end-group
        # makes the link order-independent. Both flags are GNU ld
        # (arm-none-eabi) specific and unsupported by darwin's linker.
        target_link_libraries(${_ARG_NAME} PRIVATE
            "-Wl,--start-group" ${_static_libs} "-Wl,--end-group")
    else()
        target_link_libraries(${_ARG_NAME} PRIVATE ${_static_libs})
    endif()

    foreach(_HDR IN LISTS _ARG_HEADERS)
        target_link_libraries(${_ARG_NAME} PRIVATE ${_HDR})
    endforeach()
endfunction()

# picofuse_test(<target> <source> ...)
#
# Builds a picofuse system test and registers it with CTest. The test target
# can use the same native/Pico linkage behavior as picofuse_executable().
function(picofuse_test NAME)
    if(NOT NAME)
        message(FATAL_ERROR "picofuse_test: target name is required")
    endif()
    if(NOT ARGN)
        message(FATAL_ERROR "picofuse_test: at least one source file is required")
    endif()

    set(_sources)
    foreach(_source IN LISTS ARGN)
        list(APPEND _sources "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/${_source}")
    endforeach()

    picofuse_executable(
        NAME ${NAME}
        LIBRARIES picofuse-sys
        SOURCES ${_sources}
    )
    target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ${NAME} COMMAND $<TARGET_FILE:${NAME}>)
endfunction()

# picofuse_library(NAME <target>
#                   LIBRARIES <lib> ...
#                   [HEADERS <headers-only-target> ...]
#                   SOURCES <file> ...)
#
# Same as picofuse_executable(), but produces a static library, lib<target>.a,
# instead of a linked executable — for building your own reusable code (e.g.
# a concrete picofuse/sys backend) against picosdk or any other
# picofuse_add_pico_sdk_library()/_bundle() archive. Unlike those, and unlike
# picofuse_executable(), <target> here is the real CMake target name (no
# _static suffix) — pass it directly to target_link_libraries() or as another
# picofuse_executable()'s LIBRARIES entry. No --start-group/--end-group here:
# archiving doesn't resolve symbols the way a final link does, so link order
# doesn't matter yet — that happens once whatever finally links this library
# pulls it in.
function(picofuse_library)
    cmake_parse_arguments(_ARG "" "NAME" "LIBRARIES;HEADERS;SOURCES" ${ARGN})

    if(NOT _ARG_NAME)
        message(FATAL_ERROR "picofuse_library: NAME is required")
    endif()
    if(NOT _ARG_SOURCES)
        message(FATAL_ERROR "picofuse_library: SOURCES is required")
    endif()

    add_library(${_ARG_NAME} STATIC ${_ARG_SOURCES})

    foreach(_LIB IN LISTS _ARG_LIBRARIES)
        if(NOT TARGET ${_LIB}_static)
            message(FATAL_ERROR "picofuse_library: ${_LIB} was not built via picofuse_add_pico_sdk_library()/picofuse_add_pico_sdk_bundle()")
        endif()
        target_link_libraries(${_ARG_NAME} PRIVATE ${_LIB}_static)
    endforeach()

    foreach(_HDR IN LISTS _ARG_HEADERS)
        target_link_libraries(${_ARG_NAME} PRIVATE ${_HDR})
    endforeach()

    set_property(GLOBAL APPEND PROPERTY PICOFUSE_SDK_LIBRARIES ${_ARG_NAME})
endfunction()
