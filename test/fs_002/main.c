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
// Recursively remove everything under `path` (children before parents), so
// a POSIX-path volume's real scratch directory can be rmdir'd afterward.
// Only used for the path/file backends below - a memory volume's storage
// is freed outright by fs_vol_deinit() instead.
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
  write_file(volume, "/a.txt", "hello");
  write_file(volume, "/b.txt", "world!");
  test_assert(fs_vol_mkdir(volume, "/subdir"));
}

static void run_checks(fs_volume_t *volume) {
  fs_file_t it;
  memset(&it, 0, sizeof(it));
  int count = 0;
  bool saw_a = false, saw_b = false, saw_subdir = false;
  while (fs_vol_readdir(volume, "/", &it)) {
    test_assert(strcmp(it.name, ".") != 0);
    test_assert(strcmp(it.name, "..") != 0);
    if (strcmp(it.name, "a.txt") == 0) {
      saw_a = true;
      test_assert(!it.dir);
      test_assert(it.size == 5);
    } else if (strcmp(it.name, "b.txt") == 0) {
      saw_b = true;
      test_assert(!it.dir);
      test_assert(it.size == 6);
    } else if (strcmp(it.name, "subdir") == 0) {
      saw_subdir = true;
      test_assert(it.dir);
    }
    count++;
  }
  test_assert(saw_a && saw_b && saw_subdir);
  test_assert(count == 3);

  memset(&it, 0, sizeof(it));
  test_assert(!fs_vol_readdir(volume, "/subdir", &it));

  memset(&it, 0, sizeof(it));
  test_assert(!fs_vol_readdir(volume, "/nope", &it));

  memset(&it, 0, sizeof(it));
  test_assert(!fs_vol_readdir(volume, "/a.txt", &it));
}

test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  {
    char root[] = "/tmp/picofuse_fs_002_XXXXXX";
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
    char file_path[] = "/tmp/picofuse_fs_002_file_XXXXXX";
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
