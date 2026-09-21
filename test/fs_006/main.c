#include <picofuse/fs.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
#include <stdlib.h>
#include <unistd.h>
#endif

static void write_file(fs_volume_t *volume, const char *path,
                       const char *content) {
  fs_file_t f = fs_file_create(volume, path);
  test_assert(f.ctx != NULL);
  size_t len = strlen(content);
  test_assert(fs_file_write(&f, content, len) == len);
  test_assert(fs_file_close(&f));
}

#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
// Not readdir-based (unlike the other fs_* tests' cleanup helper): readdir
// deliberately skips hidden entries, and this fixture's leftovers include
// several ("/.hidden.txt", "/.hidden_dir", "/.renamed.txt") that a
// listing-driven walk would never see. The exact fixture is small and
// known, so remove it by name instead - tolerating paths already renamed
// or removed by run_checks().
static void cleanup_all(fs_volume_t *volume) {
  fs_vol_remove(volume, "/visible.txt");
  fs_vol_remove(volume, "/.hidden.txt");
  fs_vol_remove(volume, "/visible_dir");
  fs_vol_remove(volume, "/.hidden_dir");
  fs_vol_remove(volume, "/.newhidden");
  fs_vol_remove(volume, "/.renamed.txt");
}
#endif

static void build_fixture(fs_volume_t *volume) {
  write_file(volume, "/visible.txt", "abc");
  write_file(volume, "/.hidden.txt", "xyz");
  test_assert(fs_vol_mkdir(volume, "/visible_dir"));
  test_assert(fs_vol_mkdir(volume, "/.hidden_dir"));
}

static void run_checks(fs_volume_t *volume) {
  // readdir should skip every hidden entry.
  fs_file_t it;
  memset(&it, 0, sizeof(it));
  int count = 0;
  bool saw_visible_file = false, saw_visible_dir = false;
  while (fs_vol_readdir(volume, "/", &it)) {
    test_assert(it.name[0] != '.');
    if (strcmp(it.name, "visible.txt") == 0) {
      saw_visible_file = true;
    } else if (strcmp(it.name, "visible_dir") == 0) {
      saw_visible_dir = true;
    }
    count++;
  }
  test_assert(saw_visible_file && saw_visible_dir);
  test_assert(count == 2);

  // Hidden entries must still be directly reachable by explicit path -
  // "hidden" only affects enumeration, not access.
  fs_file_t hf = fs_vol_stat(volume, "/.hidden.txt");
  test_assert(hf.name[0] != '\0');
  test_assert(!hf.dir);
  test_assert(hf.size == 3);

  fs_file_t hd = fs_vol_stat(volume, "/.hidden_dir");
  test_assert(hd.name[0] != '\0');
  test_assert(hd.dir);

  // mkdir/remove must work on hidden target names too.
  test_assert(fs_vol_mkdir(volume, "/.newhidden"));
  fs_file_t nh = fs_vol_stat(volume, "/.newhidden");
  test_assert(nh.name[0] != '\0' && nh.dir);

  // The newly created hidden directory still shouldn't show up in a
  // listing.
  memset(&it, 0, sizeof(it));
  count = 0;
  while (fs_vol_readdir(volume, "/", &it)) {
    count++;
  }
  test_assert(count == 2);

  test_assert(fs_vol_remove(volume, "/.newhidden"));
  nh = fs_vol_stat(volume, "/.newhidden");
  test_assert(nh.name[0] == '\0');

  // move must work when the destination name is hidden.
  test_assert(fs_vol_move(volume, "/visible.txt", "/.renamed.txt"));
  fs_file_t rn = fs_vol_stat(volume, "/.renamed.txt");
  test_assert(rn.name[0] != '\0' && !rn.dir && rn.size == 3);
}

test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  {
    char root[] = "/tmp/picofuse_fs_006_XXXXXX";
    test_assert(mkdtemp(root) != NULL);

    fs_volume_t *volume = fs_vol_init_path(root);
    test_assert(volume != NULL);
    build_fixture(volume);
    run_checks(volume);
    cleanup_all(volume);
    fs_vol_deinit(volume);

    test_assert(rmdir(root) == 0);
  }

  {
    char file_path[] = "/tmp/picofuse_fs_006_file_XXXXXX";
    int file_fd = mkstemp(file_path);
    test_assert(file_fd >= 0);
    close(file_fd);

    fs_volume_t *volume = fs_vol_init_file(file_path, 64 * 1024);
    test_assert(volume != NULL);
    build_fixture(volume);
    run_checks(volume);
    fs_vol_deinit(volume);

    unlink(file_path);
  }
#endif

  {
    fs_volume_t *volume = fs_vol_init_memory(NULL, 64 * 1024);
    test_assert(volume != NULL);
    build_fixture(volume);
    run_checks(volume);
    fs_vol_deinit(volume);
  }
}
