picofuse_executable(
    NAME pitft
    LIBRARIES picofuse-hw picofuse-dev
    SOURCES main.c
)
target_include_directories(pitft PRIVATE ${CMAKE_SOURCE_DIR}/include)
