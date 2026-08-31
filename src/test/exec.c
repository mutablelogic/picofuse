#include "exec.h"

#include <picofuse/sys.h>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

bool exec_openocd(const exec_openocd_opts_t *opts) {
  if (opts == NULL || opts->openocd == NULL || opts->interface == NULL ||
      opts->target == NULL || opts->elf == NULL) {
    return false;
  }

  char command[512];
  sys_sprintf(command, sizeof(command), "program %s verify reset exit",
              opts->elf);

  char *argv[] = {
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

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (!opts->verbose) {
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                      O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                      O_WRONLY, 0);
  }

  pid_t pid;
  int rc = posix_spawnp(&pid, opts->openocd, &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    sys_printf("Error: could not start '%s' (%s)\n", opts->openocd,
               strerror(rc));
    return false;
  }

  uint32_t waited_ms = 0;
  uint32_t timeout_ms = opts->timeout * 1000u;
  for (;;) {
    int status;
    pid_t got = waitpid(pid, &status, WNOHANG);
    if (got == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
      }
      if (WIFEXITED(status)) {
        sys_printf("Error: '%s' exited with status %d\n", opts->openocd,
                   WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        sys_printf("Error: '%s' was killed by signal %d\n", opts->openocd,
                   WTERMSIG(status));
      }
      return false;
    }
    if (timeout_ms > 0 && waited_ms >= timeout_ms) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0); // reap
      sys_printf("Error: '%s' timed out after %u seconds\n", opts->openocd,
                 opts->timeout);
      return false;
    }
    sys_sleep_ms(50);
    waited_ms += 50;
  }
}
