#include <hardware/regs/uart.h>
#include <hardware/uart.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// hid_register_iostream(): HID attaches to an already-open sys_iostream_t*
// (it does not create or own it - same ownership split as
// hid_register_wifi()) and forwards readiness as a hid_event_type_iostream
// event. Uses the same PL011 hardware loopback trick as test/hw_019 (TX
// wired to RX internally via CR.LBE, no external wiring) to
// deterministically trigger a real read-readiness event. Pico-only, same
// pin/baud setup as hw_019.
#define HID_IOSTREAM_TEST_TX_PIN 4
#define HID_IOSTREAM_TEST_RX_PIN 5
#define HID_IOSTREAM_TEST_BAUD 115200

test_main_hw(0) {
  hw_gpio_t *tx_pin = hw_gpio_init(0, HID_IOSTREAM_TEST_TX_PIN, hw_gpio_none);
  hw_gpio_t *rx_pin = hw_gpio_init(0, HID_IOSTREAM_TEST_RX_PIN, hw_gpio_none);
  if (tx_pin == NULL || rx_pin == NULL) {
    sys_printf("[hid_009] no GPIO backend available on this platform\n");
    return;
  }

  sys_iostream_t *uart =
      hw_uart_init(rx_pin, tx_pin, HID_IOSTREAM_TEST_BAUD, NULL);
  if (uart == NULL) {
    sys_printf("[hid_009] no UART backend available on this platform\n");
    hw_gpio_deinit(rx_pin);
    hw_gpio_deinit(tx_pin);
    return;
  }

  // PL011 hardware loopback - see test/hw_019's own comment on this trick.
  uart_get_hw(uart1)->cr |= UART_UARTCR_LBE_BITS;

  sys_event_queue_t *queue = sys_event_queue_init(16);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  int userdata_sentinel = 0;
  hid_device_t *device =
      hid_register_iostream(instance, uart, &userdata_sentinel);
  test_assert(device != NULL);

  const char *name = NULL;
  hid_type_t type = hid_type_none;
  test_assert(hid_device_info(device, &name, NULL, &type, NULL));
  test_assert_strequal(name, "iostream");
  test_assert(type == hid_type_iostream);
  test_assert(hid_device_userdata(device) == &userdata_sentinel);
  test_assert(hid_device_handle(device) == uart);

  // Write a burst - it loops back to RX in hardware, so a real
  // read-readiness event must fire.
  test_assert(sys_iostream_write(uart, "ping", 4) == 4);
  test_assert(hw_uart_flush(uart, 1000) == true);

  bool got_read_ready = false;
  uint64_t start = sys_timestamp_ms();
  while (!got_read_ready && sys_timestamp_ms() - start < 5000) {
    hw_poll();
    hid_event_t *event = (hid_event_t *)sys_event_queue_try_pop(queue);
    if (event != NULL) {
      test_assert(event->type == hid_event_type_iostream);
      test_assert(event->device == device);
      if ((event->data.iostream.events & sys_iostream_event_read) != 0) {
        got_read_ready = true;
      }
      hid_event_free(event);
    } else {
      sys_sleep_ms(10);
    }
  }
  test_assert(got_read_ready);

  // Confirm the data itself round-tripped correctly, same as hw_019's own
  // loopback check.
  char buf[8] = {0};
  size_t got = 0;
  for (int i = 0; i < 50 && got < 4; i++) {
    got += sys_iostream_read(uart, buf + got, sizeof(buf) - got);
    if (got < 4) {
      sys_sleep_ms(10);
    }
  }
  test_assert(got == 4);
  test_assert(memcmp(buf, "ping", 4) == 0);

  test_assert(hid_deregister(instance, device));

  // hid_deregister() only detached the callback - the stream is still the
  // caller's to manage. If it had wrongly called sys_iostream_close()
  // itself, this would double-close/crash.
  sys_iostream_close(uart);

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
  hw_gpio_deinit(rx_pin);
  hw_gpio_deinit(tx_pin);
}
