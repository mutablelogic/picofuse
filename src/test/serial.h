#pragma once
#include <stdbool.h>
#include <stdint.h>

// Opens a serial device (e.g. "/dev/tty.usbmodem83102") in raw mode at the
// given baud rate, ready for line-oriented reading. Returns a valid file
// descriptor, or -1 on failure.
int serial_open(const char *path, uint32_t baud);

// Opens a TCP connection to an RTT server on localhost. Returns a valid file
// descriptor, or -1 if the server is not ready.
int serial_open_rtt(uint16_t port);

// Finds an unused loopback TCP port for an RTT server. Returns false on
// socket setup failure.
bool serial_find_unused_port(uint16_t *port);

// Closes a file descriptor returned by serial_open().
void serial_close(int fd);

// Reads lines from fd, echoing each one via sys_puts() as it arrives, until
// either a line starting with success_prefix is seen (returns true), a line
// starting with fail_prefix is seen (returns false), or deadline_ms (an
// absolute sys_timestamp_ms() value) is reached with neither seen (returns
// false).
bool serial_wait_for_marker(int fd, uint64_t deadline_ms,
                            const char *success_prefix,
                            const char *fail_prefix);
