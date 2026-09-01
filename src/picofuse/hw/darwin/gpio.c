#include <picofuse/hw.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation for GPIO initialization */
hw_gpio_t *hw_gpio_init(uint8_t bank, uint8_t pin, hw_gpio_mode_t mode) {
  (void)bank;
  (void)pin;
  (void)mode;
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** Stub implementation for GPIO count */
uint8_t hw_gpio_count(uint8_t bank) {
  (void)bank;
  return 0;
}
