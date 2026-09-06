# The real lwIP-based backend needs actual network hardware - Pico's only
# network interface is the CYW43 Wi-Fi chip, and lwIP itself is only
# linked when PICOFUSE_WIFI is on (see the top-level CMakeLists.txt's
# pico_cyw43_arch_lwip_poll vs pico_cyw43_arch_none choice) - same gate
# hw/pico/CMakeLists.txt uses for its own real wifi.c.
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
        ${_net_src}
)

target_include_directories(picofuse-net PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-net PRIVATE picofuse-sys)

if(PICOFUSE_WIFI AND PICO_CYW43_SUPPORTED)
    # socket.c/listener.c pull in pico/cyw43_arch.h (transitively
    # lwip/opt.h), which needs lwipopts.h findable on the include path -
    # matches hw/pico/CMakeLists.txt's own identical requirement.
    target_include_directories(picofuse-net PRIVATE
        ${CMAKE_SOURCE_DIR}/include/runtime/pico
    )
endif()
