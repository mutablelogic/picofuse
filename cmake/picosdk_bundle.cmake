# picofuse_add_pico_sdk_library(TARGET [HEADER_DEPS ...])
#
# Compiles a pico-sdk INTERFACE library target into a real static library,
# <TARGET>_static / lib<TARGET>.a. HEADER_DEPS supplies extra "*_headers"
# targets TARGET's sources assume are available (normally provided by
# pico_runtime/pico_stdlib bundling everything together) — never pass a
# real, source-bearing target here, or its sources get compiled in too.
function(picofuse_add_pico_sdk_library TARGET)
    add_library(${TARGET}_static STATIC)
    target_sources(${TARGET}_static PRIVATE $<TARGET_PROPERTY:${TARGET},INTERFACE_SOURCES>)
    target_link_libraries(${TARGET}_static PRIVATE ${TARGET} pico_base_headers ${ARGN})
    set_target_properties(${TARGET}_static PROPERTIES OUTPUT_NAME ${TARGET})

    # Re-expose resolved include dirs/defines to consumers without
    # re-exposing the PRIVATE link graph (which would recompile the same
    # sources into anything that links this). Reading them off a throwaway
    # probe target avoids a generator-expression self-reference on
    # ${TARGET}_static, which CMake rejects outright.
    add_library(${TARGET}_usage_probe INTERFACE)
    target_link_libraries(${TARGET}_usage_probe INTERFACE ${TARGET} pico_base_headers ${ARGN})
    target_include_directories(${TARGET}_static PUBLIC
        $<TARGET_PROPERTY:${TARGET}_usage_probe,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${TARGET}_static PUBLIC
        $<TARGET_PROPERTY:${TARGET}_usage_probe,INTERFACE_COMPILE_DEFINITIONS>)
    # CMake's automatic "static library forwards its PRIVATE dependencies to
    # whoever finally links it" special case only covers LINK_LIBRARIES (the
    # other archives that also need linking) — raw linker flags set via
    # target_link_options() on a PRIVATE dependency (e.g. pico_standard_link's
    # own -T/--wrap/--gc-sections) stay private to it and never reach a real
    # consumer like an executable without this same re-exposure.
    target_link_options(${TARGET}_static PUBLIC
        $<TARGET_PROPERTY:${TARGET}_usage_probe,INTERFACE_LINK_OPTIONS>)

    set_property(GLOBAL APPEND PROPERTY PICOFUSE_SDK_LIBRARIES ${TARGET}_static)
endfunction()

# picofuse_add_pico_sdk_bundle(NAME TARGETS <sdk-target> ...
#                               [SOURCES <file> ...]
#                               [HEADER_DEPS <headers-only-target> ...])
#
# Like picofuse_add_pico_sdk_library(), but composes several targets into one
# archive — for reproducing an upstream umbrella (e.g. pico_runtime) yourself
# minus one piece it hardcodes. TARGETS entries missing on the current
# platform are skipped. SOURCES compiles specific .c files directly,
# bypassing their own upstream target when that target hardcodes an
# unwanted link (e.g. newlib_interface.c's own target unconditionally links
# pico_stdio, even though the file itself only needs it inside '#if LIB_PICO_STDIO').
function(picofuse_add_pico_sdk_bundle NAME)
    cmake_parse_arguments(_ARG "" "" "TARGETS;SOURCES;HEADER_DEPS" ${ARGN})

    set(_targets)
    foreach(_T IN LISTS _ARG_TARGETS)
        if(TARGET ${_T})
            list(APPEND _targets ${_T})
        endif()
    endforeach()

    add_library(${NAME}_static STATIC)
    foreach(_T IN LISTS _targets)
        target_sources(${NAME}_static PRIVATE $<TARGET_PROPERTY:${_T},INTERFACE_SOURCES>)
    endforeach()
    if(_ARG_SOURCES)
        target_sources(${NAME}_static PRIVATE ${_ARG_SOURCES})
    endif()
    target_link_libraries(${NAME}_static PRIVATE ${_targets} pico_base_headers ${_ARG_HEADER_DEPS})
    set_target_properties(${NAME}_static PROPERTIES OUTPUT_NAME ${NAME})

    # See picofuse_add_pico_sdk_library() for why this probe target exists.
    add_library(${NAME}_usage_probe INTERFACE)
    target_link_libraries(${NAME}_usage_probe INTERFACE ${_targets} pico_base_headers ${_ARG_HEADER_DEPS})
    target_include_directories(${NAME}_static PUBLIC
        $<TARGET_PROPERTY:${NAME}_usage_probe,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${NAME}_static PUBLIC
        $<TARGET_PROPERTY:${NAME}_usage_probe,INTERFACE_COMPILE_DEFINITIONS>)
    # See picofuse_add_pico_sdk_library() for why this is needed: PRIVATE
    # link options (e.g. pico_standard_link's own -T/--wrap/--gc-sections)
    # aren't part of CMake's automatic static-lib dependency forwarding,
    # unlike LINK_LIBRARIES.
    target_link_options(${NAME}_static PUBLIC
        $<TARGET_PROPERTY:${NAME}_usage_probe,INTERFACE_LINK_OPTIONS>)

    set_property(GLOBAL APPEND PROPERTY PICOFUSE_SDK_LIBRARIES ${NAME}_static)
endfunction()

# picofuse_add_pico_sdk_libs_target(NAME)
#
# Defines a custom target NAME depending on every library built so far via
# picofuse_add_pico_sdk_library()/picofuse_add_pico_sdk_bundle(). Call once,
# after the last such call.
function(picofuse_add_pico_sdk_libs_target NAME)
    get_property(_libs GLOBAL PROPERTY PICOFUSE_SDK_LIBRARIES)
    add_custom_target(${NAME} ALL DEPENDS ${_libs})
endfunction()
