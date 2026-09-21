picofuse_library(
    NAME picofuse-fs
    SOURCES
        ../any/pool.c
        ../any/volume.c
        ../any/file.c
        ../littlefs/mem.c
        ../littlefs/file.c
        ../littlefs/ops.c
        ../littlefs/path.c
        ../stub/vol_flash.c
        ../posix/path.c
        ../posix/volume.c
        ../posix/file.c
        ${CMAKE_SOURCE_DIR}/third_party/littlefs/lfs.c
        ${CMAKE_SOURCE_DIR}/third_party/littlefs/lfs_util.c
)

target_include_directories(picofuse-fs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/littlefs
)
target_link_libraries(picofuse-fs PRIVATE picofuse-sys)
