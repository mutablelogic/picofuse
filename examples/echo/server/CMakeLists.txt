picofuse_executable(
    NAME echo_server
    LIBRARIES picofuse-app picofuse-hw picofuse-hid picofuse-net
    SOURCES main.c
)
target_include_directories(echo_server PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Optional: export WIFI_SSID/WIFI_PASSWORD before running cmake to have
# main.c join that network - see its own comment.
if(DEFINED ENV{WIFI_SSID})
    target_compile_definitions(echo_server PRIVATE WIFI_SSID="$ENV{WIFI_SSID}")
endif()
if(DEFINED ENV{WIFI_PASSWORD})
    target_compile_definitions(echo_server PRIVATE WIFI_PASSWORD="$ENV{WIFI_PASSWORD}")
endif()
