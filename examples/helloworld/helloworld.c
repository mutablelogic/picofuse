#include <picofuse/sys.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);
  sys_puts("Hello, world!\n");
  sys_exit();
  return 0;
}
