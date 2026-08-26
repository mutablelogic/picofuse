#include <picofuse/sys.h>

// Stub replacing newlib's own puts()
int puts(const char *s) {
  sys_puts(s);
  return 0;
}
