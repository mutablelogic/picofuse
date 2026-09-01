picofuse_library(
    NAME picofuse-hw
    SOURCES
        gpio.c
        init.c
        ../gpio/gpio.c
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)
