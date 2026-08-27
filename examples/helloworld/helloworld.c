#include <picofuse/sys.h>

int main(void) {
  sys_init();
  sys_puts("Hello, world!\n");
  sys_exit();
  return 0;
}
