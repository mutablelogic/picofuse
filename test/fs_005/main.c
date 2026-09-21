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

// root/
//   file1.txt         (3 bytes)
//   dirA/
//     file2.txt        (5 bytes)
//     dirB/
//       file3.txt       (7 bytes)
static void build_fixture(fs_volume_t *volume) {
  write_file(volume, "/file1.txt", "abc");
  test_assert(fs_vol_mkdir(volume, "/dirA"));
  write_file(volume, "/dirA/file2.txt", "abcde");
  test_assert(fs_vol_mkdir(volume, "/dirA/dirB"));
  write_file(volume, "/dirA/dirB/file3.txt", "abcdefg");
}

typedef struct {
  int files;
  int dirs;
} walk_stats_t;

// Recursively walks `path` on `volume`, cross-checking every readdir()
// entry against an independent stat() of its full path.
static void walk(fs_volume_t *volume, const char *path, walk_stats_t *stats) {
  fs_file_t it;
  memset(&it, 0, sizeof(it));

  while (fs_vol_readdir(volume, path, &it)) {
    char child[512];
    size_t n = (strcmp(path, "/") == 0)
                   ? sys_sprintf(child, sizeof(child), "/%s", it.name)
                   : sys_sprintf(child, sizeof(child), "%s/%s", path, it.name);
    test_assert(n > 0 && n < sizeof(child));

    fs_file_t st = fs_vol_stat(volume, child);
    // stat should always find what readdir just reported.
    test_assert(st.name[0] != '\0');
    test_assert(st.dir == it.dir);

    if (it.dir) {
      stats->dirs++;
      walk(volume, child, stats);
    } else {
      test_assert(st.size == it.size);
      stats->files++;
    }
  }
}

static void run_checks(fs_volume_t *volume) {
  walk_stats_t stats = {0};
  walk(volume, "/", &stats);
  test_assert(stats.files == 3);
  test_assert(stats.dirs == 2);

  fs_file_t f3 = fs_vol_stat(volume, "/dirA/dirB/file3.txt");
  test_assert(f3.name[0] != '\0');
  test_assert(!f3.dir);
  test_assert(f3.size == 7);
}

test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  {
    char root[] = "/tmp/picofuse_fs_005_XXXXXX";
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
    char file_path[] = "/tmp/picofuse_fs_005_file_XXXXXX";
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
