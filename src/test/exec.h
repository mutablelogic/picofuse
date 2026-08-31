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
  const char *serial;    // serial device to read test output from, or
                          // NULL/"" to skip UART verification entirely
  uint32_t baud;         // baud rate for serial, ignored if serial is unset
} exec_openocd_opts_t;

// Runs openocd to program, verify and reset the target with opts->elf, then
// (when opts->serial is set) reads the device's own UART output looking for
// a "[TEST] [EXIT] " line (success) or a "[PANIC] " line (failure), printing
// every line as it arrives. opts->timeout bounds the whole operation -
// flashing and waiting for the UART marker together, not each separately.
// Returns false on a spawn failure, a nonzero openocd exit, a "[PANIC] "
// line, or timing out before either marker appears; true otherwise (or as
// soon as openocd exits cleanly, if opts->serial is unset).
bool exec_openocd(const exec_openocd_opts_t *opts);
