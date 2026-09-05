picofuse_executable(
    NAME wifi
    LIBRARIES picofuse-app picofuse-hw picofuse-hid
    SOURCES main.c
)
target_include_directories(wifi PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Optional: export WIFI_SSID/WIFI_PASSWORD before running cmake to have
# main.c join that network instead of just scanning - see its own comment.
if(DEFINED ENV{WIFI_SSID})
    target_compile_definitions(wifi PRIVATE WIFI_SSID="$ENV{WIFI_SSID}")
endif()
if(DEFINED ENV{WIFI_PASSWORD})
    target_compile_definitions(wifi PRIVATE WIFI_PASSWORD="$ENV{WIFI_PASSWORD}")
endif()
