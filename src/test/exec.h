#pragma once
#include <stdbool.h>
#include <stdint.h>

// Options for a single openocd flash+run invocation. interface and target
// are passed through verbatim as separate "-f" arguments (e.g.
// "interface/cmsis-dap.cfg", "target/rp2040.cfg") - exec_openocd() doesn't
// assume or add any path/extension convention of its own.
typedef struct {
  const char *openocd;   // path to the openocd binary
  const char *interface; // -f value for the interface config
  const char *target;    // -f value for the target config
  const char *elf;       // path to the .elf to flash
  uint32_t timeout;      // seconds to wait before killing a hung run
  bool verbose;          // stream openocd's own stdout/stderr through
} exec_openocd_opts_t;

// Runs openocd to program, verify and reset the target with opts->elf.
// Returns true if openocd exited with status 0 before the timeout expired,
// false on a spawn failure, a nonzero exit, or a timeout (the process is
// killed in that case).
bool exec_openocd(const exec_openocd_opts_t *opts);
