#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_uart_init()/flush() lifecycle, plus sys_iostream_read/write/peek/
// set_callback/close() against a real UART instance. Pico-only, no
// physical loopback wire required: pins 4 (TX) and 5 (RX) form a valid
// uart1 pair on RP2040/RP2350 (deliberately not uart0, which is this
// board's console UART - see PICO_DEFAULT_UART_TX_PIN/RX_PIN), but
// nothing is wired between them, so RX-side assertions only check that
// reading/peeking an idle, floating line doesn't crash - not what it
// returns.
//
// hw_uart_init() runs the UART in character mode (hardware FIFOs
// disabled) so readiness callbacks fire reliably - see hw/uart.h's own
// doc. One consequence exercised below: a burst sys_iostream_write()
// only ever accepts 1 byte at a time under load (the single holding
// register), not up to 32 the way a FIFO would - callers write in a
// loop rather than assuming a full burst is accepted atomically.
#define HW_UART_TEST_TX_PIN 4
#define HW_UART_TEST_RX_PIN 5
#define HW_UART_TEST_BAUD 115200

static volatile sys_iostream_event_t hw_uart_test_events = sys_iostream_event_none;

static void on_ready(sys_iostream_t *stream, sys_iostream_event_t events,
                     void *userdata) {
  (void)stream;
  (void)userdata;
  hw_uart_test_events |= events;
}

static void write_all(sys_iostream_t *uart, const char *buf, size_t n) {
  size_t sent = 0;
  while (sent < n) {
    sent += sys_iostream_write(uart, buf + sent, n - sent);
  }
}

test_main_hw(0) {
  // NULL-safety and an invalid (non-adjacent) pin pair are both rejected.
  test_assert(hw_uart_init(NULL, NULL, 0, NULL) == NULL);

  hw_gpio_t *tx_pin = hw_gpio_init(0, HW_UART_TEST_TX_PIN, hw_gpio_none);
  hw_gpio_t *rx_pin = hw_gpio_init(0, HW_UART_TEST_RX_PIN, hw_gpio_none);
  if (tx_pin == NULL || rx_pin == NULL) {
    sys_printf("[hw_019] no GPIO backend available on this platform\n");
    return;
  }
  test_assert(hw_uart_init(tx_pin, tx_pin, HW_UART_TEST_BAUD, NULL) == NULL);

  sys_iostream_t *uart =
      hw_uart_init(rx_pin, tx_pin, HW_UART_TEST_BAUD, NULL);
  if (uart == NULL) {
    sys_printf("[hw_019] no UART backend available on this platform\n");
    hw_gpio_deinit(rx_pin);
    hw_gpio_deinit(tx_pin);
    return;
  }

  // The singleton is per-instance (uart1, here) - a second init on the
  // same pins while this handle is still open is rejected rather than
  // silently stealing it.
  test_assert(hw_uart_init(rx_pin, tx_pin, HW_UART_TEST_BAUD, NULL) == NULL);

  // hw_uart_flush() rejects NULL and a stream from a different backend.
  test_assert(hw_uart_flush(NULL, 0) == false);
  sys_iostream_t *string_stream = sys_string_read("not a uart");
  test_assert(hw_uart_flush(string_stream, 0) == false);
  sys_iostream_close(string_stream);

  // A burst write goes out fine as long as the caller loops (see the
  // file comment on character mode), and flush() reports it fully
  // drained well within the timeout at this baud rate.
  write_all(uart, "hello", 5);
  test_assert(hw_uart_flush(uart, 1000) == true);

  // Nothing is wired to RX, so these only check for a crash-free, in-range
  // result - not a specific value.
  char buf[8];
  size_t got = sys_iostream_read(uart, buf, sizeof(buf));
  test_assert(got <= sizeof(buf));
  int peeked = sys_iostream_peek(uart);
  test_assert(peeked == SYS_IOSTREAM_EOF || (peeked >= 0 && peeked <= 255));

  // Write-readiness now fires reliably (character mode - see the file
  // comment): register a callback, write a single byte, and expect the
  // real UART IRQ to report room for more once it drains.
  test_assert(sys_iostream_set_callback(uart, on_ready, NULL) == true);
  test_assert(sys_iostream_write(uart, "!", 1) == 1);
  for (int i = 0; i < 50 && (hw_uart_test_events & sys_iostream_event_write) == 0;
       i++) {
    sys_sleep_ms(10);
  }
  test_assert((hw_uart_test_events & sys_iostream_event_write) != 0);
  test_assert(hw_uart_flush(uart, 1000) == true);
  test_assert(sys_iostream_set_callback(uart, NULL, NULL) == true);

  sys_iostream_close(uart);

  // The slot is free again once closed - a fresh init must succeed.
  sys_iostream_t *uart2 =
      hw_uart_init(rx_pin, tx_pin, HW_UART_TEST_BAUD, NULL);
  test_assert(uart2 != NULL);
  sys_iostream_close(uart2);

  hw_gpio_deinit(rx_pin);
  hw_gpio_deinit(tx_pin);
}
