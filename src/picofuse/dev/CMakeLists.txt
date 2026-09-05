picofuse_library(
    NAME picofuse-dev
    SOURCES
        bme680/bme680.c
        ft6236/ft6236.c
        stmpe610/stmpe610.c
)

target_include_directories(picofuse-dev PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(picofuse-dev PRIVATE picofuse-hw)
