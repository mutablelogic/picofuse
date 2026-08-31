#include <picofuse/sys.h>

#include "exec.h"

#include <stdlib.h>
#include <unistd.h>

#define USAGE_BUF_CAP 1024
#define PATH_BUF_CAP 1024

// Resolves openocd to a concrete, executable path, the same rule
// posix_spawnp()/execvp() use internally: a name containing a '/' is
// checked directly, otherwise each directory in $PATH is tried in turn.
// Done here rather than left to exec_openocd() so a missing binary fails
// fast with a clear message, instead of an opaque spawn error surfacing
// after everything else has already been parsed.
static bool resolve_openocd(const char *openocd, char *resolved,
                            size_t cap) {
  if (openocd == NULL || resolved == NULL || cap == 0) {
    return false;
  }

  if (sys_string_contains(openocd, "/") >= 0) {
    if (access(openocd, X_OK) != 0) {
      return false;
    }
    sys_sprintf(resolved, cap, "%s", openocd);
    return true;
  }

  const char *path = getenv("PATH");
  if (path == NULL) {
    return false;
  }

  const char *dir = path;
  while (*dir != '\0') {
    const char *end = dir;
    while (*end != '\0' && *end != ':') {
      end++;
    }
    size_t dir_len = (size_t)(end - dir);
    char candidate[PATH_BUF_CAP];
    if (dir_len > 0 && dir_len < sizeof(candidate) - 1) {
      for (size_t i = 0; i < dir_len; i++) {
        candidate[i] = dir[i];
      }
      candidate[dir_len] = '\0';
      size_t n = sys_sprintf(candidate + dir_len, sizeof(candidate) - dir_len,
                             "/%s", openocd);
      if (n < sizeof(candidate) - dir_len && access(candidate, X_OK) == 0) {
        sys_sprintf(resolved, cap, "%s", candidate);
        return true;
      }
    }
    dir = (*end == ':') ? end + 1 : end;
  }
  return false;
}

static sys_env_arg_flag_t flags[] = {
    {
        .long_name = "help",
        .short_name = "h",
        .type = SYS_ENV_ARG_BOOL,
        .value = "false",
    },
    {
        .long_name = "openocd",
        .short_name = NULL,
        .type = SYS_ENV_ARG_STRING,
        .value = "openocd",
    },
    {
        .long_name = "interface",
        .short_name = NULL,
        .type = SYS_ENV_ARG_STRING,
        .value = "interface/cmsis-dap.cfg",
    },
    {
        .long_name = "target",
        .short_name = NULL,
        .type = SYS_ENV_ARG_STRING,
        .value = NULL,
    },
    {
        // Hardcoded for now - this is the Debug Probe's UART bridge device
        // on the current dev machine, and changes across replugs/reboots or
        // on a different machine, so override with --serial as needed.
        .long_name = "serial",
        .short_name = NULL,
        .type = SYS_ENV_ARG_STRING,
        .value = "/dev/tty.usbmodem83102",
    },
    {
        .long_name = "baud",
        .short_name = NULL,
        .type = SYS_ENV_ARG_UINT,
        .value = "115200",
    },
    {
        .long_name = "timeout",
        .short_name = NULL,
        .type = SYS_ENV_ARG_UINT,
        .value = "10",
    },
    {
        .long_name = "verbose",
        .short_name = "v",
        .type = SYS_ENV_ARG_BOOL,
        .value = "false",
    },
    {0},
};

static void print_usage(void) {
  sys_printf("Usage: %s [options] <elf-file>\n\n", sys_env_name());
  char buf[USAGE_BUF_CAP];
  sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
  if (stream != NULL) {
    sys_env_arg_usage(flags, stream);
    sys_puts(buf);
    sys_iostream_close(stream);
  }
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  sys_env_arg_t *args = sys_env_arg_parse(flags);
  if (args == NULL) {
    print_usage();
    sys_exit();
    return 1;
  }

  bool help = false;
  sys_env_arg_parse_bool(args, "help", &help);
  if (help) {
    print_usage();
    sys_exit();
    return 0;
  }

  if (sys_env_arg_count(args) < 1) {
    sys_puts("Error: missing <elf-file> argument\n\n");
    print_usage();
    sys_exit();
    return 1;
  }
  const char *elf_path = sys_env_arg_string(args, 0);

  char openocd_name[256] = {0};
  sys_env_arg_parse_string(args, "openocd", openocd_name, sizeof(openocd_name));

  char openocd[PATH_BUF_CAP] = {0};
  if (!resolve_openocd(openocd_name, openocd, sizeof(openocd))) {
    sys_printf("Error: '%s' not found (checked PATH)\n", openocd_name);
    sys_exit();
    return 1;
  }

  char interface[256] = {0};
  sys_env_arg_parse_string(args, "interface", interface, sizeof(interface));

  char target[256] = {0};
  sys_env_arg_parse_string(args, "target", target, sizeof(target));
  if (target[0] == '\0') {
    sys_puts("Error: --target is required\n\n");
    print_usage();
    sys_exit();
    return 1;
  }

  uint32_t timeout = 0;
  sys_env_arg_parse_uint32(args, "timeout", &timeout);

  bool verbose = false;
  sys_env_arg_parse_bool(args, "verbose", &verbose);

  char serial[256] = {0};
  sys_env_arg_parse_string(args, "serial", serial, sizeof(serial));

  uint32_t baud = 0;
  sys_env_arg_parse_uint32(args, "baud", &baud);

  exec_openocd_opts_t opts = {
      .openocd = openocd,
      .interface = interface,
      .target = target,
      .elf = elf_path,
      .timeout = timeout,
      .verbose = verbose,
      .serial = serial,
      .baud = baud,
  };
  if (!exec_openocd(&opts)) {
    sys_exit();
    return 1;
  }

  sys_exit();
  return 0;
}
