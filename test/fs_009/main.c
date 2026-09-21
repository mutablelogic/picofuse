#include <picofuse/fs.h>
#include <string.h>
#include <test/test.h>

// Exercises only the generic dispatcher (src/picofuse/fs/any/), never a
// backend, so this runs identically on every platform - no volume is ever
// mounted.
test_main_sys(0) {
  fs_vol_deinit(NULL); // must not crash

  size_t free_bytes = 123;
  test_assert(fs_vol_size(NULL, &free_bytes) == 0);
  test_assert(free_bytes == 0);

  fs_file_t it;
  memset(&it, 0, sizeof(it));
  test_assert(!fs_vol_readdir(NULL, "/", &it));

  fs_file_t st = fs_vol_stat(NULL, "/");
  test_assert(st.name[0] == '\0');

  test_assert(!fs_vol_mkdir(NULL, "/x"));
  test_assert(!fs_vol_remove(NULL, "/x"));
  test_assert(!fs_vol_move(NULL, "/a", "/b"));

  test_assert(fs_file_create(NULL, "/x").ctx == NULL);
  test_assert(fs_file_open(NULL, "/x", false).ctx == NULL);
  test_assert(!fs_file_close(NULL));

  char buf[8];
  test_assert(fs_file_read(NULL, buf, sizeof(buf)) == 0);
  test_assert(fs_file_write(NULL, buf, sizeof(buf)) == 0);
  test_assert(!fs_file_seek(NULL, 0));
}
