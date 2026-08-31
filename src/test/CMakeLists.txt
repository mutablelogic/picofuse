picofuse_executable(
    NAME testrunner
    LIBRARIES picofuse-sys
    SOURCES testrunner.c
)
target_include_directories(testrunner PRIVATE ${CMAKE_SOURCE_DIR}/include)
