picofuse_executable(
    NAME echo_client
    LIBRARIES picofuse-sys picofuse-net
    SOURCES main.c
)
target_include_directories(echo_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
