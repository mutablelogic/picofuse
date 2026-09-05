picofuse_executable(
    NAME helloworld
    LIBRARIES picofuse-app picofuse-hw picofuse-hid
    SOURCES helloworld.c
)
target_include_directories(helloworld PRIVATE ${CMAKE_SOURCE_DIR}/include)
