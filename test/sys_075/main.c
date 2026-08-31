#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // sys_env_serial - never NULL/empty on any platform, and calling it
  // twice returns the exact same pointer (a cached/static buffer, not a
  // fresh string each time).

  {
    const char *serial = sys_env_serial();
    test_assert(serial != NULL && serial[0] != '\0');

    const char *serial2 = sys_env_serial();
    test_assert(serial2 == serial);
  }

#if defined(SYSTEM_NAME_PICO)
  // Pico's serial is a fixed-format hex string derived from the on-chip
  // flash unique ID: exactly 2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES hex
  // digits, no separators.
  {
    const char *serial = sys_env_serial();
    size_t len = strlen(serial);
    test_assert(len == 16);
    for (size_t i = 0; i < len; i++) {
      char c = serial[i];
      test_assert((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                   (c >= 'a' && c <= 'f'));
    }
  }
#endif

  ///////////////////////////////////////////////////////////////////////
  // sys_env_name - never NULL/empty, and is a short name (no path
  // separator) rather than a raw argv[0]/full path.

  {
    const char *name = sys_env_name();
    test_assert(name != NULL && name[0] != '\0');
    test_assert(strchr(name, '/') == NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_env_system - deterministic per platform, matching the compiled
  // SYSTEM_NAME identifier this project already builds with everywhere
  // else, except Pico, which (with no PICO_BOARD macro defined for this
  // board's header) falls back to the same "pico" literal.

#if defined(SYSTEM_NAME_DARWIN)
  test_assert(strcmp(sys_env_system(), "darwin") == 0);
#elif defined(SYSTEM_NAME_LINUX)
  test_assert(strcmp(sys_env_system(), "linux") == 0);
#elif defined(SYSTEM_NAME_PICO)
  test_assert(strcmp(sys_env_system(), "pico") == 0);
#endif

  ///////////////////////////////////////////////////////////////////////
  // sys_env_version - matches the compile-time PICOFUSE_VERSION macro.
  // This project's top-level CMakeLists.txt always defines it (even if
  // PROGRAM_VERSION were empty, it's still passed as an empty string,
  // not omitted), so the "unknown" fallback path isn't reachable through
  // a normal build here - Pico's own richer fallback chain (real
  // binary_info metadata, then PICO_PROGRAM_VERSION_STRING) isn't set
  // for this build either, so it lands on this same value too.

#ifdef PICOFUSE_VERSION
  test_assert(strcmp(sys_env_version(), PICOFUSE_VERSION) == 0);
#else
  test_assert(strcmp(sys_env_version(), "unknown") == 0);
#endif

}
