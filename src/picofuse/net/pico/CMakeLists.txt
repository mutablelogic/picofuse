
if(PICOFUSE_WIFI AND PICO_CYW43_SUPPORTED)
    set(_net_src socket.c listener.c)
else()
    set(_net_src ../stub/socket.c)
endif()

picofuse_library(
    NAME picofuse-net
    LIBRARIES picosdk
    SOURCES
        ../addr/addr.c
        ../ntp/ntp.c
        ../ntp/hid.c
        ../mqtt/mqtt.c
        ../mqtt/connect.c
        ../mqtt/publish.c
        ../mqtt/io.c
        ../mqtt/poll.c
        ../poll.c
        ${_net_src}
)

target_include_directories(picofuse-net PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-net PRIVATE picofuse-sys picofuse-hid)

if(PICOFUSE_WIFI AND PICO_CYW43_SUPPORTED)
    # socket.c/listener.c pull in pico/cyw43_arch.h (transitively
    # lwip/opt.h), which needs lwipopts.h findable on the include path -
    # matches hw/pico/CMakeLists.txt's own identical requirement.
    target_include_directories(picofuse-net PRIVATE
        ${CMAKE_SOURCE_DIR}/include/runtime/pico
    )
endif()
