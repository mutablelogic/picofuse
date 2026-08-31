#include <picofuse/sys.h>
#include <runtime/stdout.h>
#include <stddef.h>

void *sys_stdout = NULL;

// Stub replacing newlib's own puts()
int puts(const char *s) {
  sys_puts(s);
  return 0;
}
