picofuse_executable(
    NAME sdl
    LIBRARIES picofuse-app picofuse-hw picofuse-hid picofuse-dev
    SOURCES main.c
)
target_include_directories(sdl PRIVATE ${CMAKE_SOURCE_DIR}/include)
