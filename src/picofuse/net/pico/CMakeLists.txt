# No real lwIP-based backend exists yet (@todo - see ../stub/socket.c);
# Pico's only network interface is the CYW43 Wi-Fi chip, and lwIP itself is
# only linked when PICOFUSE_WIFI is on (see the top-level CMakeLists.txt's
# pico_cyw43_arch_lwip_poll vs pico_cyw43_arch_none choice), so once it's
# written it should sit here as socket.c, gated the same way
# hw/pico/CMakeLists.txt gates its real wifi.c:
#   if(PICOFUSE_WIFI AND PICO_CYW43_SUPPORTED)
#       set(_net_src socket.c)
#   else()
#       set(_net_src ../stub/socket.c)
#   endif()
picofuse_library(
    NAME picofuse-net
    SOURCES
        ../addr/addr.c
        ../stub/socket.c
)

target_include_directories(picofuse-net PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-net PRIVATE picofuse-sys)
