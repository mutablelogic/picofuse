picofuse_library(
    NAME picofuse-hw
    SOURCES
        gpio.c
        i2c.c
        init.c
        spi.c
        ../deviceio/deviceio.c
        ../gpio/gpio.c
        ../stub/adc.c
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)
