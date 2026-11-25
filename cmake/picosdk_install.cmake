# Captured at parse time: CMAKE_CURRENT_LIST_DIR inside the functions below
# would instead reflect the *caller's* file (functions don't push a new
# list-file scope the way include()/add_subdirectory() do).
set(_picosdk_install_dir ${CMAKE_CURRENT_LIST_DIR})

# picofuse_install()
#
# Installs everything a project outside this source tree needs to build
# against picofuse, under CMAKE_INSTALL_PREFIX:
#
#   include/picofuse/                    the public headers
#   lib/<target>/lib<name>.a             every picofuse_library()
#   lib/<target>/pkgconfig/<name>.pc     its flags and dependencies
#   cmake/picofuse_executable.cmake
#
# and, on a PICO_BOARD build only, lib/<target>/libpicosdk.a, picosdk.pc,
# picosdk-headers.pc, and (under lib/<target>/picosdk/) the SDK headers, linker
# scripts and boot2 object - see picofuse_install_picosdk().
#
# <target> is PICOFUSE_TARGET: the board, or the host's system and processor.
# The rest is the same for all of them, so any number can be installed into
# one prefix.
#
# Call once, after every picofuse_library(). Nothing installed refers to the
# source or build tree, and the .pc files find their prefix relative to
# themselves, so an installed prefix can be moved.
function(picofuse_install)
    get_property(_libs GLOBAL PROPERTY PICOFUSE_SDK_LIBRARIES)
    foreach(_lib IN LISTS _libs)
        if(_lib STREQUAL "picosdk_static")
            picofuse_install_picosdk()
        else()
            picofuse_install_library(${_lib})
        endif()
    endforeach()

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/picofuse
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")

    # The downstream-facing picofuse_executable(), never include()'d alongside
    # the in-tree one in picosdk_executable.cmake - no name collision, since a
    # consumer include()s this from their own, separate CMake process.
    install(FILES ${_picosdk_install_dir}/picofuse_executable.cmake DESTINATION cmake)
endfunction()

# picofuse_install_library(NAME)
#
# Installs the picofuse_library() target NAME, with a .pc file whose
# dependencies are read off what NAME links to, so they're only ever stated
# once. These are static libraries, which carry none of their dependencies, so
# the .pc uses Requires/Libs and not their .private forms: a consumer needn't
# remember to pass --static.
function(picofuse_install_library NAME)
    install(TARGETS ${NAME} ARCHIVE DESTINATION lib/${PICOFUSE_TARGET})

    set(_requires)
    set(_link)
    get_target_property(_deps ${NAME} LINK_LIBRARIES)
    if(NOT _deps)
        set(_deps)
    endif()
    foreach(_dep IN LISTS _deps)
        if(_dep MATCHES "^picofuse-")
            list(APPEND _requires ${_dep})
        elseif(_dep STREQUAL "picosdk_static")
            list(APPEND _requires picosdk)
        elseif(_dep MATCHES "_headers$")
            # Headers-only SDK target, needed only to compile NAME itself.
        elseif(_dep STREQUAL "OpenSSL::Crypto")
            # Prefer OpenSSL's own .pc to the path find_package() resolved,
            # which under Homebrew names an exact version.
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(PICOFUSE_LIBCRYPTO QUIET libcrypto)
            endif()
            if(PICOFUSE_LIBCRYPTO_FOUND)
                list(APPEND _requires libcrypto)
            else()
                list(APPEND _link "${OPENSSL_CRYPTO_LIBRARY}")
            endif()
        elseif(_dep STREQUAL "PkgConfig::SDL2")
            list(APPEND _requires sdl2)
        elseif(_dep STREQUAL "usb-1.0")
            list(APPEND _requires libusb-1.0)
        elseif(_dep STREQUAL "Threads::Threads")
            list(APPEND _link "-pthread")
        elseif(_dep MATCHES "::|\\$<")
            message(FATAL_ERROR "picofuse_install_library(${NAME}): don't know how to express ${_dep} in a .pc file")
        elseif(_dep MATCHES "^[-/]")
            list(APPEND _link "${_dep}")
        else()
            list(APPEND _link "-l${_dep}")
        endif()
    endforeach()

    set(_requires_line "")
    if(_requires)
        list(REMOVE_DUPLICATES _requires)
        list(JOIN _requires " " _requires)
        set(_requires_line "Requires: ${_requires}\n")
    endif()
    list(JOIN _link " " _link)

    # SYSTEM_NAME_* because the public headers test it (see sys/hash.h), so a
    # consumer has to agree with how the library itself was built.
    picofuse_pkgconfig_version(_version)
    set(_pc ${CMAKE_BINARY_DIR}/picofuse_package/${NAME}.pc)
    file(WRITE ${_pc}
"prefix=\${pcfiledir}/../../..
libdir=\${prefix}/lib/${PICOFUSE_TARGET}
includedir=\${prefix}/include

Name: ${NAME}
Description: ${NAME} library for ${PICOFUSE_TARGET}
Version: ${_version}
${_requires_line}Cflags: -I\${includedir} -DSYSTEM_NAME_${SYSTEM_NAME_UPPER}
Libs: -L\${libdir} -l${NAME} ${_link}
")
    install(FILES ${_pc} DESTINATION lib/${PICOFUSE_TARGET}/pkgconfig)
endfunction()

# picofuse_install_picosdk()
#
# Installs the picosdk bundle as two packages:
#
#   picosdk          what's needed to link: the archive, boot2, the linker
#                    scripts and flags, plus the CPU flags every translation
#                    unit has to share. Every picofuse library requires it.
#   picosdk-headers  the SDK's headers and defines. Nothing requires it, so
#                    raw SDK headers never leak into a consumer that only
#                    asked for picofuse.
function(picofuse_install_picosdk)
    install(TARGETS picosdk_static ARCHIVE DESTINATION lib/${PICOFUSE_TARGET})

    # Only generator expressions can see a target's fully-resolved transitive
    # properties, and only at CMake's generate step - so capture them to files
    # now; the text-processing happens later, from install(CODE).
    set(_capture_dir ${CMAKE_BINARY_DIR}/picofuse_package/picosdk)
    foreach(_prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_OPTIONS)
        file(GENERATE OUTPUT ${_capture_dir}/${_prop}.txt
            CONTENT "$<TARGET_PROPERTY:picosdk_static,${_prop}>")
    endforeach()

    # boot2 reaches an in-tree executable as an object file, through
    # pico_standard_link's LINK_LIBRARIES. It's never a member of the archive.
    set(_boot2)
    if(TARGET bs2_default_library)
        set(_boot2 ${_capture_dir}/boot2.txt)
        file(GENERATE OUTPUT ${_boot2} CONTENT "$<TARGET_OBJECTS:bs2_default_library>")
    endif()

    picofuse_pkgconfig_version(_version)
    install(CODE "
        include([[${_picosdk_install_dir}/picosdk_install_helpers.cmake]])
        picofuse_write_picosdk(
            VERSION [[${_version}]]
            BOARD [[${PICO_BOARD}]]
            PLATFORM [[${PICO_PLATFORM}]]
            CPU_FLAGS [[${CMAKE_C_FLAGS}]]
            CAPTURE_DIR [[${_capture_dir}]]
            BOOT2_FILE [[${_boot2}]]
            SDK_DIR [[${PICO_SDK_PATH}/src]]
            BUILD_SDK_DIR [[${CMAKE_BINARY_DIR}/pico-sdk/src]]
            PROJECT_INCLUDE_DIR [[${CMAKE_SOURCE_DIR}/include]]
            PREFIX \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\"
        )
    ")
endfunction()

function(picofuse_pkgconfig_version OUT)
    if(PROGRAM_VERSION)
        set(${OUT} "${PROGRAM_VERSION}" PARENT_SCOPE)
    else()
        set(${OUT} "0.0.0" PARENT_SCOPE)
    endif()
endfunction()
