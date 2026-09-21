#include <picofuse/fs.h>
#include <picofuse/sys.h>
#include <test/test.h>

#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
#include <stdlib.h>
#include <unistd.h>
#endif

static void run_checks(fs_volume_t *volume) {
  size_t free_bytes = 0;
  size_t total = fs_vol_size(volume, &free_bytes);
  test_assert(total > 0);
  test_assert(free_bytes <= total);

  size_t total_again = fs_vol_size(volume, NULL);
  test_assert(total_again == total);
}

test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  {
    char root[] = "/tmp/picofuse_fs_004_XXXXXX";
    test_assert(mkdtemp(root) != NULL);

    fs_volume_t *volume = fs_vol_init_path(root);
    test_assert(volume != NULL);
    run_checks(volume);
    fs_vol_deinit(volume);

    test_assert(rmdir(root) == 0);
  }

  {
    char file_path[] = "/tmp/picofuse_fs_004_file_XXXXXX";
    int file_fd = mkstemp(file_path);
    test_assert(file_fd >= 0);
    close(file_fd);

    fs_volume_t *volume = fs_vol_init_file(file_path, 64 * 1024);
    test_assert(volume != NULL);
    run_checks(volume);
    fs_vol_deinit(volume);

    unlink(file_path);
  }
#endif

  {
    fs_volume_t *volume = fs_vol_init_memory(NULL, 64 * 1024);
    test_assert(volume != NULL);
    run_checks(volume);
    fs_vol_deinit(volume);
  }

  size_t null_free = 123;
  test_assert(fs_vol_size(NULL, &null_free) == 0);
  test_assert(null_free == 0);
}
