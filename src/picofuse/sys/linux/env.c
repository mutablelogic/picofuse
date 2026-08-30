#define _GNU_SOURCE

#include <picofuse/sys.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

const char *sys_env_serial(void) {
  static char serial[128];

  if (serial[0] != '\0') {
    return serial;
  }

  FILE *file = fopen("/etc/machine-id", "r");
  if (file != NULL) {
    if (fgets(serial, sizeof(serial), file) != NULL) {
      size_t length = strlen(serial);
      if (length > 0 && serial[length - 1] == '\n') {
        serial[length - 1] = '\0';
      }
    }
    fclose(file);
  }

  if (serial[0] == '\0') {
    memcpy(serial, "unknown", sizeof("unknown"));
  }

  return serial;
}

const char *sys_env_name(void) {
  const char *name = program_invocation_short_name;
  return (name && *name) ? name : "unknown";
}
