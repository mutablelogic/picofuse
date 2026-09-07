picofuse_library(
    NAME picofuse-net
    SOURCES
        ../addr/addr.c
        ../ntp/ntp.c
        ../ntp/hid.c
        ../posix/socket.c
        ../posix/listener.c
)

target_include_directories(picofuse-net PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-net PRIVATE picofuse-sys picofuse-hid)
