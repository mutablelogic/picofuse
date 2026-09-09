picofuse_library(
    NAME picofuse-pix
    SOURCES
        display.c
        poll.c
)

target_include_directories(picofuse-pix PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-pix PRIVATE picofuse-sys)
