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

// Runs OpenOCD to program, verify, and reset opts->elf. An explicit
// opts->serial reads test markers from the UART bridge; otherwise it starts
// OpenOCD's RTT channel-0 TCP server and reads markers from there. opts->timeout
// bounds flashing and marker collection together. Returns false on setup,
// OpenOCD, or marker failure, including a "[PANIC] " line or timeout.
bool exec_openocd(const exec_openocd_opts_t *opts);
