#include <picofuse/hw.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no UART hardware on this platform. */
sys_iostream_t *hw_uart_init(const hw_gpio_t *rx_pin, const hw_gpio_t *tx_pin,
                             uint32_t baud_rate,
                             const hw_uart_config_t *config) {
  (void)rx_pin;
  (void)tx_pin;
  (void)baud_rate;
  (void)config;
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** Stub implementation: no UART hardware on this platform. */
bool hw_uart_flush(sys_iostream_t *uart, uint32_t timeout_ms) {
  (void)uart;
  (void)timeout_ms;
  return false;
}
