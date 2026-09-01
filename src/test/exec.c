#include "exec.h"
#include "serial.h"

#include <picofuse/sys.h>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RTT_ADDRESS 0x20000000
#define RTT_SCAN_SIZE 0x40000

extern char **environ;

static void stop_openocd(pid_t pid) {
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
}

bool exec_openocd(const exec_openocd_opts_t *opts) {
  if (opts == NULL || opts->openocd == NULL || opts->interface == NULL ||
      opts->target == NULL || opts->elf == NULL) {
    return false;
  }

  // A single deadline covers flashing and marker collection.
  uint64_t deadline_ms = sys_timestamp_ms() + (uint64_t)opts->timeout * 1000u;

  int serial_fd = -1;
  bool want_serial = opts->serial != NULL && opts->serial[0] != '\0';
  uint16_t rtt_port = 0;
  if (want_serial) {
    serial_fd = serial_open(opts->serial, opts->baud);
    if (serial_fd < 0) {
      sys_printf("Error: could not open serial port '%s'\n", opts->serial);
      return false;
    }
  } else if (!serial_find_unused_port(&rtt_port)) {
    sys_puts("Error: could not find an unused RTT port\n");
    return false;
  }

  char command[512];
  sys_sprintf(command, sizeof(command), "program %s verify reset%s", opts->elf,
              want_serial ? " exit" : "");
  char rtt_setup[128];
  sys_sprintf(rtt_setup, sizeof(rtt_setup),
              "rtt setup 0x%x 0x%x \"SEGGER RTT\"", (unsigned int)RTT_ADDRESS,
              (unsigned int)RTT_SCAN_SIZE);
  char rtt_server[64];
  sys_sprintf(rtt_server, sizeof(rtt_server), "rtt server start %u 0",
              (unsigned int)rtt_port);

  char *argv_uart[] = {
      (char *)opts->openocd,
      "-f",
      (char *)opts->interface,
      "-f",
      (char *)opts->target,
      // Without this, openocd defaults to a very low 100 kHz adapter
      // speed and warns about it - matches .vscode/launch.json's own
      // openOCDLaunchCommands setting.
      "-c",
      "adapter speed 5000",
      "-c",
      command,
      NULL,
  };
  char *argv_rtt[] = {
      (char *)opts->openocd,
      "-f",
      (char *)opts->interface,
      "-f",
      (char *)opts->target,
      "-c",
      "adapter speed 5000",
      "-c",
      command,
      "-c",
      rtt_setup,
      "-c",
      "rtt start",
      "-c",
      rtt_server,
      NULL,
  };

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (!opts->verbose) {
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                     O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);
  }

  pid_t pid;
  int rc = posix_spawnp(&pid, opts->openocd, &actions, NULL,
                        want_serial ? argv_uart : argv_rtt, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    sys_printf("Error: could not start '%s' (%s)\n", opts->openocd,
               strerror(rc));
    serial_close(serial_fd);
    return false;
  }

  if (!want_serial) {
    // The RTT TCP server is created only after OpenOCD has programmed and
    // reset the target, so a successful connection confirms flash setup.
    while (sys_timestamp_ms() < deadline_ms) {
      serial_fd = serial_open_rtt(rtt_port);
      if (serial_fd >= 0) {
        break;
      }

      int status;
      pid_t got = waitpid(pid, &status, WNOHANG);
      if (got == pid) {
        sys_printf("Error: '%s' exited with status %d\n", opts->openocd,
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
      }
      sys_sleep_ms(50);
    }
    if (serial_fd < 0) {
      stop_openocd(pid);
      sys_puts("Error: timed out waiting for OpenOCD RTT server\n");
      return false;
    }

    bool ok = serial_wait_for_marker(serial_fd, deadline_ms, "[TEST] [EXIT] ",
                                     "[PANIC] ");
    serial_close(serial_fd);
    stop_openocd(pid);
    return ok;
  }

  bool flashed = false;
  for (;;) {
    int status;
    pid_t got = waitpid(pid, &status, WNOHANG);
    if (got == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        flashed = true;
      } else if (WIFEXITED(status)) {
        sys_printf("Error: '%s' exited with status %d\n", opts->openocd,
                   WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        sys_printf("Error: '%s' was killed by signal %d\n", opts->openocd,
                   WTERMSIG(status));
      }
      break;
    }
    if (sys_timestamp_ms() >= deadline_ms) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0); // reap
      sys_printf("Error: '%s' timed out after %u seconds\n", opts->openocd,
                 opts->timeout);
      serial_close(serial_fd);
      return false;
    }
    sys_sleep_ms(50);
  }

  if (!flashed) {
    serial_close(serial_fd);
    return false;
  }
  bool ok = serial_wait_for_marker(serial_fd, deadline_ms, "[TEST] [EXIT] ",
                                   "[PANIC] ");
  serial_close(serial_fd);
  return ok;
}
