picofuse_executable(
    NAME helloworld
    LIBRARIES picofuse_sys_pico
    SOURCES helloworld.c
)
target_include_directories(helloworld PRIVATE ${CMAKE_SOURCE_DIR}/include)
