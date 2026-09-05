#include <picofuse/hw.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no GPIO hardware on this platform. */
hw_gpio_t *hw_gpio_init(uint8_t bank, uint8_t pin, hw_gpio_mode_t mode) {
  (void)bank;
  (void)pin;
  (void)mode;
  return NULL;
}

/** Stub implementation: no GPIO hardware on this platform. */
void hw_gpio_deinit(hw_gpio_t *gpio) { (void)gpio; }

/** Stub implementation: no board-default user button on this platform. */
hw_gpio_t *hw_gpio_init_userbutton(void) { return NULL; }

/** Stub implementation: no GPIO pins on this platform. */
uint8_t hw_gpio_count(uint8_t bank) {
  (void)bank;
  return 0;
}

/** Stub implementation: no GPIO hardware on this platform. */
void hw_gpio_set_callback(hw_gpio_callback_t callback, void *userdata) {
  (void)callback;
  (void)userdata;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** Stub implementation: no GPIO hardware on this platform. */
uint8_t hw_gpio_pin(const hw_gpio_t *gpio) {
  (void)gpio;
  return 0;
}

/** Stub implementation: no GPIO hardware on this platform. */
uint8_t hw_gpio_bank(const hw_gpio_t *gpio) {
  (void)gpio;
  return 0;
}

/** Stub implementation: no GPIO hardware on this platform. */
hw_gpio_mode_t hw_gpio_get_mode(const hw_gpio_t *gpio) {
  (void)gpio;
  return hw_gpio_unknown;
}

/** Stub implementation: no GPIO hardware on this platform. */
void hw_gpio_set_mode(hw_gpio_t *gpio, hw_gpio_mode_t mode) {
  (void)gpio;
  (void)mode;
}

/** Stub implementation: no GPIO hardware on this platform. */
bool hw_gpio_get(const hw_gpio_t *gpio) {
  (void)gpio;
  return false;
}

/** Stub implementation: no GPIO hardware on this platform. */
void hw_gpio_set(hw_gpio_t *gpio, bool value) {
  (void)gpio;
  (void)value;
}
