picofuse_executable(
    NAME flags
    LIBRARIES picofuse-sys
    SOURCES main.c
)
target_include_directories(flags PRIVATE ${CMAKE_SOURCE_DIR}/include)
