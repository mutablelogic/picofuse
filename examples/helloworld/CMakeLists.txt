picofuse_executable(
    NAME helloworld
    LIBRARIES picofuse-sys
    SOURCES helloworld.c
)
target_include_directories(helloworld PRIVATE ${CMAKE_SOURCE_DIR}/include)
