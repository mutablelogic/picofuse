#include <picofuse/fs.h>
#include <picofuse/sys.h>
#include <test/test.h>

#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
#include <stdlib.h>
#include <unistd.h>
#endif

// fs_vol_init_{path,file,memory}() basics, including each backend's
// documented "unsupported on this platform" contract.
test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  char cwd[1024];
  test_assert(getcwd(cwd, sizeof(cwd)) != NULL);

  fs_volume_t *volume = fs_vol_init_path(cwd);
  test_assert(volume != NULL);
  fs_vol_deinit(volume);
#else
  test_assert(fs_vol_init_path("/") == NULL);
#endif

#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  // A host file-backed volume should mount, creating and formatting a
  // fresh image when the file doesn't already hold one.
  char file_path[] = "/tmp/picofuse_fs_001_file_XXXXXX";
  int file_fd = mkstemp(file_path);
  test_assert(file_fd >= 0);
  close(file_fd);

  fs_volume_t *file_vol = fs_vol_init_file(file_path, 64 * 1024);
  test_assert(file_vol != NULL);
  fs_vol_deinit(file_vol);
  unlink(file_path);

  // A NULL/empty path names no file at all and must be rejected outright.
  test_assert(fs_vol_init_file(NULL, 64 * 1024) == NULL);
  test_assert(fs_vol_init_file("", 64 * 1024) == NULL);
#else
  test_assert(fs_vol_init_file("/some/path", 64 * 1024) == NULL);
#endif

  // A RAM-backed volume should mount unconditionally on every platform.
  fs_volume_t *mem = fs_vol_init_memory(NULL, 64 * 1024);
  test_assert(mem != NULL);
  fs_vol_deinit(mem);

  // size == 0 should round up to at least one block rather than fail.
  fs_volume_t *tiny = fs_vol_init_memory(NULL, 0);
  test_assert(tiny != NULL);
  fs_vol_deinit(tiny);

  // Arena-backed allocation is not implemented yet - any non-NULL arena
  // must be rejected rather than silently falling back to the heap.
  sys_mem_arena_t *arena =
      sys_mem_arena_init(4096, NULL, sys_malloc, sys_free);
  test_assert(arena != NULL);
  test_assert(fs_vol_init_memory(arena, 4096) == NULL);
  sys_mem_arena_delete(arena);
}
