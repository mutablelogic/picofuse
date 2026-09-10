picofuse_library(
    NAME picofuse-pix
    SOURCES
        bitmap.c
        display.c
        mono.c
        poll.c
        rgb565.c
        rgb888.c
        rgba32.c
)

target_include_directories(picofuse-pix PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-pix PRIVATE picofuse-sys)
