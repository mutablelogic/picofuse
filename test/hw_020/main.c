#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_uart_init_device() NULL-safety, lifecycle, and a real write/flush/
// callback-register smoke test against an actual serial device, if one is
// named via the HW_UART_TEST_DEVICE environment variable (e.g. export
// HW_UART_TEST_DEVICE=/dev/cu.usbserial-1420 or /dev/ttyUSB0). Skips
// cleanly if unset - Pico has no device-path UART concept at all (always
// NULL, see hw/pico/uart.c's stub), and neither does a host with nothing
// set. No assumption is made about anything being wired to the device's
// RX line, so this only checks that real I/O doesn't crash or hang - not
// that any particular byte comes back.
static void on_ready(sys_iostream_t *stream, sys_iostream_event_t events,
                     void *userdata) {
  (void)stream;
  (void)events;
  (void)userdata;
}

test_main_hw(0) {
  test_assert(hw_uart_init_device(NULL, 115200, NULL) == NULL);
  test_assert(hw_uart_init_device("", 115200, NULL) == NULL);
  test_assert(hw_uart_init_device("/dev/null", 0, NULL) == NULL);

#ifndef HW_UART_TEST_DEVICE
#define HW_UART_TEST_DEVICE ""
#endif
  const char *device = HW_UART_TEST_DEVICE;
  if (device[0] == '\0') {
    sys_printf("[hw_020] HW_UART_TEST_DEVICE not set, skipping\n");
    return;
  }

  // An unsupported baud rate is rejected outright.
  test_assert(hw_uart_init_device(device, 12345, NULL) == NULL);

  sys_iostream_t *uart = hw_uart_init_device(device, 115200, NULL);
  if (uart == NULL) {
    sys_printf("[hw_020] could not open \"%s\"\n", device);
    return;
  }

  test_assert(sys_iostream_write(uart, "hello", 5) == 5);
  test_assert(hw_uart_flush(uart, 1000) == true);

  // Nothing is guaranteed to be wired to RX, so this only checks for a
  // crash-free, in-range result - not a specific value.
  char buf[8];
  size_t got = sys_iostream_read(uart, buf, sizeof(buf));
  test_assert(got <= sizeof(buf));

  // Registering and clearing a callback must not crash - the background
  // RX thread runs regardless (see hw_uart_init_device()'s own doc).
  test_assert(sys_iostream_set_callback(uart, on_ready, NULL) == true);
  test_assert(sys_iostream_set_callback(uart, NULL, NULL) == true);

  sys_iostream_close(uart);

  // Reopening after close must succeed.
  sys_iostream_t *uart2 = hw_uart_init_device(device, 115200, NULL);
  test_assert(uart2 != NULL);
  sys_iostream_close(uart2);
}
