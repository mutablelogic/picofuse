# picofuse_install_picotool()
#
# Builds picotool from third_party/picotool, and installs it as bin/picotool.
# It runs on the host, so this isn't for a PICO_BOARD build.
#
# Only the program is installed: it has its data files compiled in as well.
function(picofuse_install_picotool)
    set(_source ${CMAKE_SOURCE_DIR}/third_party/picotool)
    set(_sdk ${CMAKE_SOURCE_DIR}/third_party/pico-sdk)
    set(_build ${CMAKE_BINARY_DIR}/picotool)
    foreach(_dir ${_source} ${_sdk})
        if(NOT EXISTS ${_dir}/CMakeLists.txt)
            message(WARNING "${_dir} is missing (git submodule update --init --recursive): not building picotool")
            return()
        endif()
    endforeach()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PICOTOOL_LIBUSB QUIET libusb-1.0)
    endif()
    if(NOT PICOTOOL_LIBUSB_FOUND)
        message(WARNING "libusb-1.0 not found: picotool won't be able to access devices")
    endif()

    set(_args
        -DCMAKE_BUILD_TYPE=Release
        -DPICO_SDK_PATH=${_sdk}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    )
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND _args -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif()

    include(ExternalProject)
    ExternalProject_Add(picotool
        SOURCE_DIR ${_source}
        BINARY_DIR ${_build}
        CMAKE_ARGS ${_args}
        INSTALL_COMMAND ""
    )

    install(PROGRAMS ${_build}/picotool DESTINATION bin)
    if(APPLE)
        # There's no system libusb on a Mac, so install a copy of the one
        # picotool is linked with, and point picotool at it. Changing a binary
        # invalidates its signature, without which it won't run.
        install(CODE "
            set(_prefix \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
            set(_picotool \"\${_prefix}/bin/picotool\")
            execute_process(COMMAND otool -L \"\${_picotool}\" OUTPUT_VARIABLE _libs COMMAND_ERROR_IS_FATAL ANY)
            if(_libs MATCHES \"[\\n\\t ]+(/[^ \\n]*/(libusb[^ /\\n]*))\")
                set(_lib \"\${CMAKE_MATCH_1}\")
                set(_name \"\${CMAKE_MATCH_2}\")
                set(_dest \"\${_prefix}/lib/${PICOFUSE_TARGET}\")
                file(REAL_PATH \"\${_lib}\" _real)
                file(MAKE_DIRECTORY \"\${_dest}\")
                file(COPY_FILE \"\${_real}\" \"\${_dest}/\${_name}\")
                file(CHMOD \"\${_dest}/\${_name}\" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
                execute_process(COMMAND install_name_tool -id \"@rpath/\${_name}\" \"\${_dest}/\${_name}\" ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
                execute_process(COMMAND install_name_tool -change \"\${_lib}\" \"@loader_path/../lib/${PICOFUSE_TARGET}/\${_name}\" \"\${_picotool}\" ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
                foreach(_file \"\${_dest}/\${_name}\" \"\${_picotool}\")
                    execute_process(COMMAND codesign --force --sign - \"\${_file}\" ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
                endforeach()
            endif()
        ")
    endif()
endfunction()
