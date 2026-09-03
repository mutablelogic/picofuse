# picofuse_executable(NAME <target>
#                      LIBRARIES <lib> ...
#                      [HEADERS <headers-only-target> ...]
#                      [VERSION <version>]
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
#
# VERSION defaults to PROGRAM_VERSION (the project's own version, exposed to
# C code elsewhere as PICOFUSE_VERSION) when not given. It's always set as
# this target's CMake VERSION property (mostly cosmetic for an executable).
# On a PICO_BOARD build it also becomes this target's *own*
# bi_program_version_string() binary_info entry, generated into a tiny
# per-target source file compiled only into this executable — not, say,
# pico_standard_binary_info's own standard_binary_info.c, which this project
# bundles once into the shared picosdk archive, so a PICO_PROGRAM_VERSION_STRING
# compile definition on any one executable would never reach it. Read back at
# runtime via sys_env_version() (picofuse/sys/env.h), whose Pico backend scans
# binary_info before falling back to the project-wide PICOFUSE_VERSION - so
# this is what makes that per-executable rather than one fixed value.
function(picofuse_executable)
    cmake_parse_arguments(_ARG "" "NAME;VERSION" "LIBRARIES;HEADERS;SOURCES" ${ARGN})

    if(NOT _ARG_NAME)
        message(FATAL_ERROR "picofuse_executable: NAME is required")
    endif()
    if(NOT _ARG_SOURCES)
        message(FATAL_ERROR "picofuse_executable: SOURCES is required")
    endif()
    if(NOT _ARG_VERSION)
        set(_ARG_VERSION "${PROGRAM_VERSION}")
    endif()

    add_executable(${_ARG_NAME} ${_ARG_SOURCES})

    if(_ARG_VERSION)
        set_target_properties(${_ARG_NAME} PROPERTIES VERSION "${_ARG_VERSION}")
    endif()

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

        # PICO_ON_DEVICE (and pico.h itself) normally reach a translation
        # unit via pico_platform_headers, an INTERFACE dependency of
        # pico_base_headers. picofuse_add_pico_sdk_bundle() only forwards
        # bundled targets' INTERFACE_SOURCES into picosdk_static, not their
        # INTERFACE_COMPILE_DEFINITIONS/usage requirements, so anything
        # compiled directly into this executable (rather than folded into the
        # bundle) never sees them. Without PICO_ON_DEVICE,
        # pico/binary_info.h's bi_decl() silently expands to nothing (see
        # binary_info.h: !PICO_ON_DEVICE implies PICO_NO_BINARY_INFO), which
        # is what this target's generated *_name_bi.c/*_version_bi.c sources
        # below rely on.
        target_link_libraries(${_ARG_NAME} PRIVATE pico_base_headers)

        # pico-sdk static libs genuinely call into each other in both
        # directions (e.g. hardware_gpio's panic() calls pico_stdio's wrapped
        # puts()), which a single left-to-right archive scan can miss
        # depending on link order. Wrapping them in --start-group/--end-group
        # makes the link order-independent. Both flags are GNU ld
        # (arm-none-eabi) specific and unsupported by darwin's linker.
        target_link_libraries(${_ARG_NAME} PRIVATE
            "-Wl,--start-group" ${_static_libs} "-Wl,--end-group")

        if(_ARG_VERSION)
            set(_version_src "${CMAKE_CURRENT_BINARY_DIR}/${_ARG_NAME}_version_bi.c")
            file(WRITE "${_version_src}"
                "#include <pico/binary_info.h>\n"
                "bi_decl(bi_program_version_string(\"${_ARG_VERSION}\"))\n"
            )
            target_sources(${_ARG_NAME} PRIVATE "${_version_src}")
            target_link_libraries(${_ARG_NAME} PRIVATE pico_binary_info_headers)
        endif()

        # Same reasoning as the version string above: pico_standard_binary_info's
        # own bi_program_name() (in standard_binary_info.c) is gated on
        # PICO_TARGET_NAME, but that file is compiled once into the shared
        # picosdk archive, so no per-executable define ever reaches it. Emit
        # our own bi_program_name() into a tiny per-target source file instead,
        # so sys_env_name() (picofuse/sys/env.h) can read back this target's
        # real name via binary_info at runtime instead of falling back to
        # "unknown".
        set(_name_src "${CMAKE_CURRENT_BINARY_DIR}/${_ARG_NAME}_name_bi.c")
        file(WRITE "${_name_src}"
            "#include <pico/binary_info.h>\n"
            "bi_decl(bi_program_name(\"${_ARG_NAME}\"))\n"
        )
        target_sources(${_ARG_NAME} PRIVATE "${_name_src}")
        target_link_libraries(${_ARG_NAME} PRIVATE pico_binary_info_headers)
    else()
        target_link_libraries(${_ARG_NAME} PRIVATE ${_static_libs})
    endif()

    foreach(_HDR IN LISTS _ARG_HEADERS)
        target_link_libraries(${_ARG_NAME} PRIVATE ${_HDR})
    endforeach()
endfunction()

# picofuse_test(<target> [TESTRUNNER_TIMEOUT <seconds>] [LIBRARIES <lib> ...] <source> ...)
#
# Builds a picofuse system test and registers it with CTest. <source> paths
# are relative to the calling CMakeLists.txt's directory (CMAKE_CURRENT_SOURCE_DIR),
# same as any other CMake SOURCES list. The test target can use the same
# native/Pico linkage behavior as picofuse_executable().
#
# TESTRUNNER_TIMEOUT overrides testrunner's own default 10s wait (on
# PICO_BOARD builds only - ignored on host, which runs the test binary
# directly with no such wait) for a test whose own body genuinely needs
# longer, rather than the default being too short because something hung.
#
# LIBRARIES links additional picofuse_library() targets alongside the
# always-linked picofuse-sys - e.g. LIBRARIES picofuse-hw for a test that
# uses hw's API directly, or LIBRARIES picofuse-hw picofuse-dev for one
# exercising a driver module built on hw_deviceio_t. Most sys_* tests need
# nothing here; anything touching <picofuse/hw.h> needs picofuse-hw named
# explicitly - it's no longer pulled in by default. Put LIBRARIES after
# the source file(s) in the call, since it's a multi-value keyword that
# would otherwise swallow them.
function(picofuse_test NAME)
    if(NOT NAME)
        message(FATAL_ERROR "picofuse_test: target name is required")
    endif()

    cmake_parse_arguments(_ARG "" "TESTRUNNER_TIMEOUT" "LIBRARIES" ${ARGN})
    if(NOT _ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "picofuse_test: at least one source file is required")
    endif()

    set(_sources)
    foreach(_source IN LISTS _ARG_UNPARSED_ARGUMENTS)
        list(APPEND _sources "${CMAKE_CURRENT_SOURCE_DIR}/${_source}")
    endforeach()

    # LIBRARIES here is additional to picofuse-sys (always linked, below) -
    # see the LIBRARIES doc above.
    picofuse_executable(
        NAME ${NAME}
        LIBRARIES picofuse-sys ${_ARG_LIBRARIES}
        SOURCES ${_sources}
    )
    target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)

    if(DEFINED PICO_BOARD)
        # A pico .elf can't run directly on the (host) machine driving
        # CTest, so route it through testrunner, which flashes it onto real
        # hardware via openocd and reports pass/fail from the device's own
        # serial output. testrunner is a host tool built by this project's
        # separate host build tree (see README/build docs), not by this
        # PICO_BOARD-configured one, so its path is taken from a cache
        # variable rather than a target this build knows how to produce.
        set(PICOFUSE_TESTRUNNER "${CMAKE_SOURCE_DIR}/build/src/test/testrunner"
            CACHE FILEPATH "Path to the host-built testrunner tool used to run PICO_BOARD tests via openocd")

        if(PICO_RP2040)
            set(_pico_openocd_chip "rp2040")
        elseif(PICO_RP2350)
            set(_pico_openocd_chip "rp2350")
        else()
            set(_pico_openocd_chip "${PICO_PLATFORM}")
        endif()

        set(_testrunner_args --target "target/${_pico_openocd_chip}.cfg")
        if(_ARG_TESTRUNNER_TIMEOUT)
            list(APPEND _testrunner_args --timeout ${_ARG_TESTRUNNER_TIMEOUT})
        endif()

        add_test(NAME ${NAME} COMMAND ${PICOFUSE_TESTRUNNER}
            $<TARGET_FILE:${NAME}> ${_testrunner_args})
    else()
        add_test(NAME ${NAME} COMMAND $<TARGET_FILE:${NAME}>)
    endif()
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
