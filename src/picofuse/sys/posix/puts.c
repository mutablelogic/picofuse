#include <picofuse/sys.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void sys_puts(const char *str) {
  if (str != NULL && *str != '\0') {
    fputs(str, stdout);
  }
  fflush(stdout);
}

void sys_putch(const char ch) {
  if (ch != '\0') {
    fputc(ch, stdout);
  } else {
    fflush(stdout);
  }
}
