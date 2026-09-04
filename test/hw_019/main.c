#include <hardware/regs/uart.h>
#include <hardware/uart.h>
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
// doc - and backs it with a software ring buffer by default to restore
// burst read()/write() throughput. hw_uart_config_t.unbuffered opts out
// of that buffer, falling back to the raw 1-byte-per-call behavior -
// exercised separately below.
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

  // The default software ring buffer absorbs a short burst atomically,
  // even though the hardware itself only holds 1 byte at a time, and
  // flush() reports it fully drained (buffer and hardware) well within
  // the timeout at this baud rate.
  test_assert(sys_iostream_write(uart, "hello", 5) == 5);
  test_assert(hw_uart_flush(uart, 1000) == true);

  // Nothing is wired to RX, so these only check for a crash-free, in-range
  // result - not a specific value.
  char buf[8];
  size_t got = sys_iostream_read(uart, buf, sizeof(buf));
  test_assert(got <= sizeof(buf));
  int peeked = sys_iostream_peek(uart);
  test_assert(peeked == SYS_IOSTREAM_EOF || (peeked >= 0 && peeked <= 255));

  // Write-readiness fires reliably (character mode - see the file
  // comment): register a callback, write a burst, and expect the real
  // UART IRQ to report room for more once it's drained.
  test_assert(sys_iostream_set_callback(uart, on_ready, NULL) == true);
  test_assert(sys_iostream_write(uart, "more!", 5) == 5);
  for (int i = 0; i < 50 && (hw_uart_test_events & sys_iostream_event_write) == 0;
       i++) {
    sys_sleep_ms(10);
  }
  test_assert((hw_uart_test_events & sys_iostream_event_write) != 0);
  test_assert(hw_uart_flush(uart, 1000) == true);
  test_assert(sys_iostream_set_callback(uart, NULL, NULL) == true);

  sys_iostream_close(uart);

  // hw_uart_config_t.unbuffered opts out of the ring buffer entirely -
  // a burst write is then only ever accepted 1 byte at a time, since
  // the hardware itself has nowhere else to put it under load.
  hw_uart_config_t unbuffered_config = {
      .data_bits = hw_uart_data_bits_8,
      .stop_bits = hw_uart_stop_bits_1,
      .parity = hw_uart_parity_none,
      .flow_control = hw_uart_flow_control_none,
      .unbuffered = true,
  };
  sys_iostream_t *raw_uart =
      hw_uart_init(rx_pin, tx_pin, HW_UART_TEST_BAUD, &unbuffered_config);
  test_assert(raw_uart != NULL);
  size_t raw_written = sys_iostream_write(raw_uart, "hello", 5);
  test_assert(raw_written >= 1 && raw_written < 5);
  test_assert(hw_uart_flush(raw_uart, 1000) == true);
  sys_iostream_close(raw_uart);

  // The slot is free again once closed - a fresh init must succeed.
  sys_iostream_t *uart2 =
      hw_uart_init(rx_pin, tx_pin, HW_UART_TEST_BAUD, NULL);
  test_assert(uart2 != NULL);

  // Loopback: PL011 hardware loopback (CR.LBE) internally connects TX to
  // RX without any external wiring, letting this section deterministically
  // verify actual byte-for-byte data integrity - something no test above
  // can check, since nothing is physically wired between pins 4/5. Pokes
  // the raw SDK register directly (test-only - not worth a hw_uart_config_t
  // knob that exists purely to make testing easier).
  uart_get_hw(uart1)->cr |= UART_UARTCR_LBE_BITS;
  test_assert(sys_iostream_write(uart2, "ping", 4) == 4);
  test_assert(hw_uart_flush(uart2, 1000) == true);

  char loopback_buf[8] = {0};
  size_t loopback_got = 0;
  for (int i = 0; i < 50 && loopback_got < 4; i++) {
    loopback_got += sys_iostream_read(uart2, loopback_buf + loopback_got,
                                      sizeof(loopback_buf) - loopback_got);
    if (loopback_got < 4) {
      sys_sleep_ms(10);
    }
  }
  test_assert(loopback_got == 4);
  test_assert(memcmp(loopback_buf, "ping", 4) == 0);

  sys_iostream_close(uart2);

  hw_gpio_deinit(rx_pin);
  hw_gpio_deinit(tx_pin);
}
