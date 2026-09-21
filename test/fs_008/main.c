#include <picofuse/fs.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
#include <stdlib.h>
#include <unistd.h>
#endif

static void run_checks(fs_volume_t *volume) {
  // Create a new file and write to it; size/pos should track the write.
  fs_file_t f = fs_file_create(volume, "/hello.txt");
  test_assert(f.ctx != NULL);
  test_assert(!f.dir);
  test_assert(f.size == 0 && f.pos == 0);
  test_assert(strcmp(f.name, "hello.txt") == 0);

  size_t n = fs_file_write(&f, "Hello, world!", 13);
  test_assert(n == 13);
  test_assert(f.pos == 13 && f.size == 13);

  test_assert(fs_file_close(&f));
  test_assert(f.ctx == NULL);

  // fs_vol_stat should agree with what was written.
  fs_file_t st = fs_vol_stat(volume, "/hello.txt");
  test_assert(st.size == 13);

  // Reopen read-only and read the content back.
  fs_file_t r = fs_file_open(volume, "/hello.txt", false);
  test_assert(r.ctx != NULL);
  test_assert(r.size == 13);

  char buf[32] = {0};
  size_t rn = fs_file_read(&r, buf, sizeof(buf));
  test_assert(rn == 13);
  test_assert(memcmp(buf, "Hello, world!", 13) == 0);
  test_assert(r.pos == 13);

  // Reading past EOF returns 0, not an error.
  test_assert(fs_file_read(&r, buf, sizeof(buf)) == 0);

  // Seeking and a partial read.
  test_assert(fs_file_seek(&r, 7));
  test_assert(r.pos == 7);
  memset(buf, 0, sizeof(buf));
  rn = fs_file_read(&r, buf, 6);
  test_assert(rn == 6);
  test_assert(memcmp(buf, "world!", 6) == 0);

  test_assert(fs_file_close(&r));

  // A read-only handle must reject writes.
  fs_file_t ro = fs_file_open(volume, "/hello.txt", false);
  test_assert(ro.ctx != NULL);
  test_assert(fs_file_write(&ro, "X", 1) == 0);
  fs_file_close(&ro);

  // create() truncates an existing file to zero length.
  fs_file_t trunc = fs_file_create(volume, "/hello.txt");
  test_assert(trunc.ctx != NULL);
  test_assert(trunc.size == 0);
  fs_file_close(&trunc);
  st = fs_vol_stat(volume, "/hello.txt");
  test_assert(st.size == 0);

  // open(write=true) allows writing to an existing file - a different
  // code path (O_RDWR open) than create()'s truncate-and-open.
  fs_file_t w = fs_file_open(volume, "/hello.txt", true);
  test_assert(w.ctx != NULL);
  test_assert(w.size == 0);
  n = fs_file_write(&w, "abcdef", 6);
  test_assert(n == 6);
  test_assert(w.pos == 6 && w.size == 6);
  test_assert(fs_file_close(&w));

  st = fs_vol_stat(volume, "/hello.txt");
  test_assert(st.size == 6);

  // open() rejects a nonexistent file.
  fs_file_t missing = fs_file_open(volume, "/nope.txt", false);
  test_assert(missing.ctx == NULL);

  // create() rejects a missing parent directory.
  fs_file_t bad_parent = fs_file_create(volume, "/missing/deep.txt");
  test_assert(bad_parent.ctx == NULL);

  test_assert(fs_vol_remove(volume, "/hello.txt"));
}

test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  {
    char root[] = "/tmp/picofuse_fs_008_XXXXXX";
    test_assert(mkdtemp(root) != NULL);

    fs_volume_t *volume = fs_vol_init_path(root);
    test_assert(volume != NULL);
    run_checks(volume);
    fs_vol_deinit(volume);

    test_assert(rmdir(root) == 0);
  }

  {
    char file_path[] = "/tmp/picofuse_fs_008_file_XXXXXX";
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

  // The generic dispatcher must be NULL-safe, independent of any backend.
  char buf[8];
  test_assert(fs_file_create(NULL, "/x").ctx == NULL);
  test_assert(fs_file_open(NULL, "/x", false).ctx == NULL);
  test_assert(fs_file_read(NULL, buf, sizeof(buf)) == 0);
  test_assert(fs_file_write(NULL, "x", 1) == 0);
  test_assert(!fs_file_seek(NULL, 0));
  test_assert(!fs_file_close(NULL));
}
