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
static void cleanup_all(fs_volume_t *volume, const char *path) {
  typedef struct {
    char name[FS_PATH_MAX + 1];
    bool dir;
  } entry_t;
  entry_t entries[32];
  int count = 0;

  fs_file_t it;
  memset(&it, 0, sizeof(it));
  while (fs_vol_readdir(volume, path, &it)) {
    test_assert(count < 32);
    strcpy(entries[count].name, it.name);
    entries[count].dir = it.dir;
    count++;
  }

  for (int i = 0; i < count; i++) {
    char child[512];
    size_t n = (strcmp(path, "/") == 0)
                   ? sys_sprintf(child, sizeof(child), "/%s", entries[i].name)
                   : sys_sprintf(child, sizeof(child), "%s/%s", path,
                                 entries[i].name);
    test_assert(n > 0 && n < sizeof(child));
    if (entries[i].dir) {
      cleanup_all(volume, child);
    }
    test_assert(fs_vol_remove(volume, child));
  }
}
#endif

static void build_fixture(fs_volume_t *volume) {
  write_file(volume, "/file_a.txt", "AAA");
  write_file(volume, "/file_b.txt", "BBBBB");
  write_file(volume, "/file_c.txt", "C");
  test_assert(fs_vol_mkdir(volume, "/dir_empty"));
  test_assert(fs_vol_mkdir(volume, "/dir_empty2"));
  test_assert(fs_vol_mkdir(volume, "/dir_full"));
  write_file(volume, "/dir_full/inner.txt", "inner");
}

// Move/rename overwrite semantics match rename(2)/littlefs semantics on
// both backends: same-type targets are atomically replaced, type mismatches
// and non-empty directory targets are rejected, and the volume root can
// never be overwritten.
static void run_checks(fs_volume_t *volume) {
  // Same-type overwrite (file -> existing file) succeeds, matching
  // rename(2)/littlefs semantics: the destination is atomically replaced.
  test_assert(fs_vol_move(volume, "/file_a.txt", "/file_b.txt"));
  fs_file_t st = fs_vol_stat(volume, "/file_a.txt");
  test_assert(st.name[0] == '\0');
  st = fs_vol_stat(volume, "/file_b.txt");
  test_assert(st.name[0] != '\0' && !st.dir && st.size == 3);

  // Type mismatch (file -> existing directory) is rejected.
  test_assert(!fs_vol_move(volume, "/file_c.txt", "/dir_empty"));
  test_assert(fs_vol_stat(volume, "/file_c.txt").name[0] != '\0');
  test_assert(fs_vol_stat(volume, "/dir_empty").dir);

  // Type mismatch (directory -> existing file) is rejected.
  test_assert(!fs_vol_move(volume, "/dir_empty", "/file_c.txt"));
  test_assert(fs_vol_stat(volume, "/dir_empty").dir);
  test_assert(!fs_vol_stat(volume, "/file_c.txt").dir);

  // Same-type overwrite (directory -> existing EMPTY directory) succeeds.
  test_assert(fs_vol_move(volume, "/dir_empty", "/dir_empty2"));
  test_assert(fs_vol_stat(volume, "/dir_empty").name[0] == '\0');
  test_assert(fs_vol_stat(volume, "/dir_empty2").dir);

  // Directory -> existing NON-EMPTY directory is rejected.
  test_assert(!fs_vol_move(volume, "/dir_empty2", "/dir_full"));
  test_assert(fs_vol_stat(volume, "/dir_empty2").dir);
  st = fs_vol_stat(volume, "/dir_full/inner.txt");
  test_assert(st.name[0] != '\0');

  // The volume root may never be overwritten by a move.
  test_assert(!fs_vol_move(volume, "/file_c.txt", "/"));
  test_assert(fs_vol_stat(volume, "/file_c.txt").name[0] != '\0');

  // Moving a path onto itself is a well-defined no-op success.
  test_assert(fs_vol_move(volume, "/file_c.txt", "/file_c.txt"));
  test_assert(fs_vol_stat(volume, "/file_c.txt").name[0] != '\0');
}

test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  {
    char root[] = "/tmp/picofuse_fs_007_XXXXXX";
    test_assert(mkdtemp(root) != NULL);

    fs_volume_t *volume = fs_vol_init_path(root);
    test_assert(volume != NULL);
    build_fixture(volume);
    run_checks(volume);
    cleanup_all(volume, "/");
    fs_vol_deinit(volume);

    test_assert(rmdir(root) == 0);
  }

  {
    char file_path[] = "/tmp/picofuse_fs_007_file_XXXXXX";
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
