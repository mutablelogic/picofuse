# Captured at parse time: CMAKE_CURRENT_LIST_DIR inside the function below
# would instead reflect the *caller's* file (functions don't push a new
# list-file scope the way include()/add_subdirectory() do).
set(_picosdk_install_dir ${CMAKE_CURRENT_LIST_DIR})

# picofuse_install_package(NAME [DESCRIPTION <text>] [VERSION <ver>]
#                           [REQUIRES <pkg> ...] [REQUIRES_PRIVATE <pkg> ...]
#                           [PUBLIC_HEADER_DIRS <subdir-of-project-include> ...])
#
# Installs <NAME>_static (built via picofuse_add_pico_sdk_library()/_bundle()/
# picofuse_library()) as a standalone, pkg-config-discoverable package under
# CMAKE_INSTALL_PREFIX: lib/lib<NAME>.a, whatever linker-script fragments it
# needs under lib/picosdk/src, and lib/pkgconfig/<NAME>.pc capturing all of
# it. A consumer elsewhere then just needs `pkg-config <NAME>` — nothing it
# doesn't already carry.
#
# Without PUBLIC_HEADER_DIRS, this package is "unscoped": its full
# transitive header set merges into the shared include/ tree, and its .pc
# Cflags is a single -I there — appropriate for an SDK bundle like picosdk,
# whose whole point is exposing that surface. With PUBLIC_HEADER_DIRS, only
# those specific subdirectories of the project's own include/ get installed,
# into a package-private lib/<NAME>/include instead — for a package like
# picofuse_sys_pico whose own actual API is just picofuse/, not the raw SDK
# headers its own INTERFACE_INCLUDE_DIRECTORIES also happens to carry
# (privately linking picosdk_static to build it pulls those in too).
#
# REQUIRES_PRIVATE (unlike REQUIRES) never becomes a pkg-config
# Requires.private: line at all — pkgconf (the pkg-config implementation
# actually in use) merges a private dependency's Cflags into a consumer's
# normal --cflags output regardless, only gating its Libs behind --static,
# which is backwards from what's needed here (picosdk's Libs, to link, but
# not its Cflags/headers). It only drives dedup: this package's own
# captured includes/defines/linkopts get REQUIRES_PRIVATE's subtracted out
# so they aren't baked in twice. Getting picosdk's Libs onto a consumer is
# handled explicitly by picofuse_executable.cmake instead, which
# pkg_check_modules()s both packages directly.
function(picofuse_install_package NAME)
    cmake_parse_arguments(_ARG "" "DESCRIPTION;VERSION" "REQUIRES;REQUIRES_PRIVATE;PUBLIC_HEADER_DIRS" ${ARGN})
    if(NOT _ARG_DESCRIPTION)
        set(_ARG_DESCRIPTION "picofuse ${NAME}")
    endif()
    if(NOT _ARG_VERSION)
        set(_ARG_VERSION "0.1.0")
    endif()

    set(_static ${NAME}_static)
    if(NOT TARGET ${_static})
        message(FATAL_ERROR "picofuse_install_package: ${NAME} was not built via picofuse_add_pico_sdk_library()/picofuse_add_pico_sdk_bundle()/picofuse_library()")
    endif()

    # Only generator expressions can see a target's fully-resolved
    # transitive properties, and only at CMake's generate step — so capture
    # them to files now; the actual .pc text-processing (LINKER:X -> -Wl,X,
    # path rewriting) happens later, from install(CODE), where real string
    # processing is available.
    set(_capture_dir ${CMAKE_BINARY_DIR}/picofuse_package/${NAME})
    file(GENERATE OUTPUT ${_capture_dir}/includes.txt CONTENT "$<TARGET_PROPERTY:${_static},INCLUDE_DIRECTORIES>")
    file(GENERATE OUTPUT ${_capture_dir}/defines.txt CONTENT "$<TARGET_PROPERTY:${_static},COMPILE_DEFINITIONS>")
    file(GENERATE OUTPUT ${_capture_dir}/linkopts.txt CONTENT "$<TARGET_PROPERTY:${_static},LINK_OPTIONS>")

    # bs2_default_library's compiled boot2 object reaches an in-tree
    # executable only via CMake's real LINK_LIBRARIES forwarding through
    # pico_standard_link (INTERFACE) -> ${_static} (STATIC) -> the final
    # executable — never folded into ${_static}'s own archive, so
    # install(TARGETS ...) alone won't carry it. Capture its build-tree path
    # directly (rather than trying to discover it generically out of
    # LINK_LIBRARIES) so it can be copied into the install tree and wired
    # into this package's own Libs: below.
    set(_boot2_object_file "")
    if(TARGET bs2_default_library)
        set(_boot2_object_file ${_capture_dir}/boot2.txt)
        file(GENERATE OUTPUT ${_boot2_object_file} CONTENT "$<TARGET_OBJECTS:bs2_default_library>")
    endif()

    install(TARGETS ${_static} ARCHIVE DESTINATION lib)

    list(JOIN _ARG_REQUIRES " " _requires_str)
    list(JOIN _ARG_REQUIRES_PRIVATE " " _requires_private_str)
    list(JOIN _ARG_PUBLIC_HEADER_DIRS " " _public_header_dirs_str)

    # A required package's own .pc already carries these via pkg-config's
    # own dependency resolution (Requires:/Requires.private:) — passing its
    # capture dir lets picofuse_write_pkgconfig() subtract whatever it
    # already provides, rather than duplicating it verbatim in this
    # package's own Cflags/Libs.
    set(_requires_capture_dirs "")
    foreach(_req IN LISTS _ARG_REQUIRES _ARG_REQUIRES_PRIVATE)
        list(APPEND _requires_capture_dirs "${CMAKE_BINARY_DIR}/picofuse_package/${_req}")
    endforeach()
    list(JOIN _requires_capture_dirs " " _requires_capture_dirs_str)

    install(CODE "
        include(\"${_picosdk_install_dir}/picosdk_install_helpers.cmake\")
        picofuse_write_pkgconfig(
            NAME [[${NAME}]]
            VERSION [[${_ARG_VERSION}]]
            DESCRIPTION [[${_ARG_DESCRIPTION}]]
            REQUIRES [[${_requires_str}]]
            REQUIRES_PRIVATE [[${_requires_private_str}]]
            REQUIRES_CAPTURE_DIRS [[${_requires_capture_dirs_str}]]
            PUBLIC_HEADER_DIRS [[${_public_header_dirs_str}]]
            INCLUDES_FILE [[${_capture_dir}/includes.txt]]
            DEFINES_FILE [[${_capture_dir}/defines.txt]]
            LINKOPTS_FILE [[${_capture_dir}/linkopts.txt]]
            BOOT2_OBJECT_FILE [[${_boot2_object_file}]]
            SDK_SRC_DIR [[${PICO_SDK_PATH}/src]]
            BUILD_SDK_DIR [[${CMAKE_BINARY_DIR}/pico-sdk/src]]
            PROJECT_INCLUDE_DIR [[${CMAKE_SOURCE_DIR}/include]]
            CPU_FLAGS [[${CMAKE_C_FLAGS}]]
            PREFIX \"\${CMAKE_INSTALL_PREFIX}\"
        )
    ")
endfunction()

# The downstream-facing picofuse_executable() (finds picofuse_sys_pico via
# pkg-config, unrelated to and never include()'d alongside the in-tree
# picofuse_executable() in picosdk_executable.cmake — no name collision,
# since a downstream consumer include()s this from their own, separate
# CMake process).
install(FILES ${_picosdk_install_dir}/picofuse_executable.cmake DESTINATION cmake)
