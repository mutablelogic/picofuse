#include "picofuse/sys/panicf.h"

// Stub: ignores the message and just halts, honoring sys_panicf's
// documented "does not return" contract without producing any output.
void sys_panicf(const char *format, ...) {
  (void)format;
  for (;;) {
  }
}
