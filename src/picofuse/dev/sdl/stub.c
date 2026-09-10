#include <picofuse/dev/sdl.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: SDL2 wasn't found at configure time (see
 * ../CMakeLists.txt) - no SDL display backend on this build. */
pix_display_t *dev_sdl_init(const char *title, pix_size_t size,
                            pix_format_t format, dev_sdl_flags_t flags,
                            uint16_t interval_ms) {
  (void)title;
  (void)size;
  (void)format;
  (void)flags;
  (void)interval_ms;
  sys_debugf("sdl", "dev_sdl_init: SDL2 backend not available on this build");
  return NULL;
}
