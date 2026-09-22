picofuse_executable(
    NAME 
        rotel
    LIBRARIES
        picofuse-app
        picofuse-net
    SOURCES
        main.c
        rotel.c
)
target_include_directories(rotel PRIVATE ${CMAKE_SOURCE_DIR}/include)
