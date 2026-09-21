#include <picofuse/fs.h>
#include <string.h>
#include <test/test.h>

// Exercises fs_vol_init_flash() against real flash hardware - Pico only
// (see CMakeLists.txt), like hw_026's own hw_block_flash_init() exercise.
// Kept to a single, non-exhaustive check rather than mirroring every other
// fs_* test against flash too: every mount/format/erase cycle here wears
// real NOR flash, which has finite write endurance.

static void write_file(fs_volume_t *volume, const char *path,
                       const char *content) {
  fs_file_t f = fs_file_create(volume, path);
  test_assert(f.ctx != NULL);
  size_t len = strlen(content);
  test_assert(fs_file_write(&f, content, len) == len);
  test_assert(fs_file_close(&f));
}

test_main_hw(0) {
  // fs_vol_init_flash() allocates its region relative to this size on every
  // call (see volume.h), so the remount below must request the same value
  // to find the filesystem formatted here rather than a different region.
  const size_t volume_size = 16 * 1024;

  fs_volume_t *volume = fs_vol_init_flash(volume_size);
  test_assert(volume != NULL);

  size_t free_bytes = 0;
  size_t total = fs_vol_size(volume, &free_bytes);
  test_assert(total > 0);
  test_assert(free_bytes <= total);

  write_file(volume, "/hello.txt", "flash!");
  fs_file_t st = fs_vol_stat(volume, "/hello.txt");
  test_assert(st.name[0] != '\0' && !st.dir && st.size == 6);

  fs_file_t r = fs_file_open(volume, "/hello.txt", false);
  test_assert(r.ctx != NULL);
  char buf[16] = {0};
  size_t n = fs_file_read(&r, buf, sizeof(buf));
  test_assert(n == 6 && memcmp(buf, "flash!", 6) == 0);
  fs_file_close(&r);

  test_assert(fs_vol_mkdir(volume, "/sub"));
  test_assert(fs_vol_stat(volume, "/sub").dir);
  test_assert(fs_vol_remove(volume, "/sub"));
  test_assert(fs_vol_stat(volume, "/sub").name[0] == '\0');

  // hw_block_flash_init() reserves from whatever flash remains after
  // earlier reservations (see hw/flash.h), so a second concurrent call
  // succeeds too, mounting a distinct, freshly formatted region - not
  // aliasing `volume`'s content.
  fs_volume_t *concurrent = fs_vol_init_flash(volume_size);
  test_assert(concurrent != NULL);
  test_assert(concurrent != volume);
  test_assert(fs_vol_stat(concurrent, "/hello.txt").name[0] == '\0');
  fs_vol_deinit(concurrent);

  fs_vol_deinit(volume);

  // Remounting with the same size should find the same, already-formatted
  // filesystem - confirming persistence across a real flash-backed
  // deinit/reinit, not just a fresh format each time.
  fs_volume_t *remounted = fs_vol_init_flash(volume_size);
  test_assert(remounted != NULL);
  st = fs_vol_stat(remounted, "/hello.txt");
  test_assert(st.name[0] != '\0' && st.size == 6);

  // Clean up so repeated flashes of this test don't accumulate content (and
  // thus unnecessary wear) across runs.
  test_assert(fs_vol_remove(remounted, "/hello.txt"));
  fs_vol_deinit(remounted);
}
