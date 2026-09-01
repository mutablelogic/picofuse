#include <picofuse/sys.h>

void sys_puts(const char *str) {
  if (str != NULL) {
    size_t len = 0;
    while (str[len] != '\0') {
      len++;
    }
    sys_iostream_write(sys_stdout, str, len);
  }
}

void sys_putch(const char ch) {
  if (ch != '\0') {
    sys_iostream_write(sys_stdout, &ch, 1);
  }
}
