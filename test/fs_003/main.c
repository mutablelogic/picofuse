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
  test_assert(fs_vol_mkdir(volume, "/sub"));
  write_file(volume, "/sub/inner.txt", "x");
  write_file(volume, "/file.txt", "x");
}

// Both backends must reject the same malformed/relative/escaping path
// forms - the POSIX backend confines against the real host filesystem, and
// the littlefs backend confines symbolically against its own volume root,
// but the documented contract (leading '/', no bare "."/"..", no climbing
// above the volume root) is identical either way.
static void run_checks(fs_volume_t *volume) {
  // Volume paths must be root-relative and start with '/'; bare "." and
  // ".." are not accepted forms.
  fs_file_t st = fs_vol_stat(volume, ".");
  test_assert(st.name[0] == '\0');

  st = fs_vol_stat(volume, "..");
  test_assert(st.name[0] == '\0');

  st = fs_vol_stat(volume, "relative");
  test_assert(st.name[0] == '\0');

  // NULL/empty both mean "root", per the documented contract.
  st = fs_vol_stat(volume, NULL);
  test_assert(st.dir);

  fs_file_t root_st = fs_vol_stat(volume, "/");
  test_assert(root_st.dir);
  test_assert(strcmp(st.name, root_st.name) == 0);

  st = fs_vol_stat(volume, "");
  test_assert(st.dir && strcmp(st.name, root_st.name) == 0);

  // Escaping above the volume root must fail rather than leak state from
  // beyond it.
  st = fs_vol_stat(volume, "/..");
  test_assert(st.name[0] == '\0');

  st = fs_vol_stat(volume, "/../../../../../../etc/passwd");
  test_assert(st.name[0] == '\0');

  // ".." that stays within the confined tree is legitimate and should
  // resolve back to root.
  st = fs_vol_stat(volume, "/sub/..");
  test_assert(st.dir && strcmp(st.name, root_st.name) == 0);

  // "/." canonicalizes to the volume root itself (which already exists),
  // so it succeeds via the documented "already exists" path rather than
  // ever reaching leaf-name validation.
  test_assert(fs_vol_mkdir(volume, "/."));

  // "/.." has no existing parent to resolve within the confined tree, so
  // it falls through to leaf-name validation, which rejects ".." outright.
  test_assert(!fs_vol_mkdir(volume, "/.."));

  // A leaf of "." or ".." under a parent that doesn't exist yet must still
  // be rejected (both for the missing parent and for the invalid leaf).
  test_assert(!fs_vol_mkdir(volume, "/missing/."));
  test_assert(!fs_vol_mkdir(volume, "/missing/.."));

  test_assert(!fs_vol_mkdir(volume, "relative"));

  // A name that already exists as a regular file is not a directory, so
  // mkdir must reject it rather than silently reporting success.
  test_assert(!fs_vol_mkdir(volume, "/file.txt"));

  // The root itself already exists, so mkdir("/") reports success without
  // trying to create anything.
  test_assert(fs_vol_mkdir(volume, "/"));

  // The root must never be removed or moved out from under the volume.
  test_assert(!fs_vol_remove(volume, "/"));
  test_assert(!fs_vol_remove(volume, "/.."));
  test_assert(!fs_vol_move(volume, "/", "/elsewhere"));

  // remove() must reject a non-empty directory and a nonexistent path.
  test_assert(!fs_vol_remove(volume, "/sub"));
  test_assert(!fs_vol_remove(volume, "/nope"));

  fs_file_t it;
  memset(&it, 0, sizeof(it));
  test_assert(!fs_vol_readdir(volume, ".", &it));
}

test_main_sys(0) {
#if defined(SYSTEM_NAME_LINUX) || defined(SYSTEM_NAME_DARWIN)
  {
    char root[] = "/tmp/picofuse_fs_003_XXXXXX";
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
    char file_path[] = "/tmp/picofuse_fs_003_file_XXXXXX";
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
