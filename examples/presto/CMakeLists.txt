picofuse_executable(
    NAME presto
    LIBRARIES picofuse-app picofuse-hw picofuse-hid
    SOURCES main.c
)
target_include_directories(presto PRIVATE ${CMAKE_SOURCE_DIR}/include)
