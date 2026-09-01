#include <picofuse/sys.h>

sys_env_arg_flag_t args[] = {
    {
        .long_name = "help",
        .short_name = "h",
        .type = sys_env_arg_type_bool,
        .value = "false",
    },
    {0},
};

int main(int argc, char *argv[]) {
  sys_init(argc, argv, 0, sys_stdio_none);
  sys_puts("Hello, world!\n");
  sys_exit();
  return 0;
}
