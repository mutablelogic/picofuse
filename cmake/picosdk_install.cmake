# Captured at parse time: CMAKE_CURRENT_LIST_DIR inside the function below
# would instead reflect the *caller's* file (functions don't push a new
# list-file scope the way include()/add_subdirectory() do).
set(_picosdk_install_dir ${CMAKE_CURRENT_LIST_DIR})

# picofuse_install_package(NAME [DESCRIPTION <text>] [VERSION <ver>] [REQUIRES <pkg> ...])
#
# Installs <NAME>_static (built via picofuse_add_pico_sdk_library()/_bundle()/
# picofuse_library()) as a standalone, pkg-config-discoverable package under
# CMAKE_INSTALL_PREFIX: lib/lib<NAME>.a, its full transitive header set
# merged into include/, whatever linker-script fragments it needs under
# lib/picosdk/src, and lib/pkgconfig/<NAME>.pc capturing all of it. A
# consumer elsewhere then just needs `pkg-config <NAME>` — nothing it
# doesn't already carry.
function(picofuse_install_package NAME)
    cmake_parse_arguments(_ARG "" "DESCRIPTION;VERSION" "REQUIRES" ${ARGN})
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

    install(TARGETS ${_static} ARCHIVE DESTINATION lib)

    list(JOIN _ARG_REQUIRES " " _requires_str)

    # A required package's own .pc already carries these via pkg-config's
    # own dependency resolution (Requires:) — passing its capture dir lets
    # picofuse_write_pkgconfig() subtract whatever it already provides,
    # rather than duplicating it verbatim in this package's own Cflags/Libs.
    set(_requires_capture_dirs "")
    foreach(_req IN LISTS _ARG_REQUIRES)
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
            REQUIRES_CAPTURE_DIRS [[${_requires_capture_dirs_str}]]
            INCLUDES_FILE [[${_capture_dir}/includes.txt]]
            DEFINES_FILE [[${_capture_dir}/defines.txt]]
            LINKOPTS_FILE [[${_capture_dir}/linkopts.txt]]
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
