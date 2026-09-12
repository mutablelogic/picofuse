if(PICOFUSE_WIFI)
    set(_net_src ../posix/socket.c ../posix/listener.c)
else()
    set(_net_src ../stub/socket.c)
endif()

picofuse_library(
    NAME picofuse-net
    SOURCES
        ../addr/addr.c
        ../ntp/ntp.c
        ../ntp/hid.c
        ../mqtt/mqtt.c
        ../mqtt/connect.c
        ../mqtt/publish.c
        ../mqtt/io.c
        ../mqtt/poll.c
        ../mqtt/subscribe.c
        ../poll.c
        ${_net_src}
)

target_include_directories(picofuse-net PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-net PRIVATE picofuse-sys picofuse-hid)
