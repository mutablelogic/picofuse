picofuse_library(
    NAME picofuse-hw
    SOURCES
        init.c
        ../deviceio/deviceio.c
        ../stub/adc.c
        ../stub/gpio.c
        ../stub/i2c.c
        ../stub/spi.c
)

target_include_directories(picofuse-hw PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-hw PRIVATE picofuse-sys)
