picofuse_executable(
    NAME stdin
    LIBRARIES picofuse-sys
    SOURCES main.c
)
target_include_directories(stdin PRIVATE ${CMAKE_SOURCE_DIR}/include)