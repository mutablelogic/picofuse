#include <picofuse/hw.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no Infrared hardware on this platform. */
hw_infrared_t *hw_infrared_init(const hw_gpio_t *rx_pin,
                                const hw_gpio_t *tx_pin,
                                const hw_infrared_config_t *config) {
  (void)rx_pin;
  (void)tx_pin;
  (void)config;
  return NULL;
}

/** Stub implementation: no Infrared hardware on this platform. */
hw_infrared_t *hw_infrared_init_device(const char *rx_device,
                                       const char *tx_device,
                                       const hw_infrared_config_t *config) {
  (void)rx_device;
  (void)tx_device;
  (void)config;
  return NULL;
}

/** Stub implementation: no Infrared hardware on this platform. */
void hw_infrared_deinit(hw_infrared_t *ir) { (void)ir; }

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** Stub implementation: no Infrared hardware on this platform. */
bool hw_infrared_set_callback(hw_infrared_t *ir, hw_infrared_callback_t callback,
                              void *userdata) {
  (void)ir;
  (void)callback;
  (void)userdata;
  return false;
}

/** Stub implementation: no Infrared hardware on this platform. */
bool hw_infrared_transmit(hw_infrared_t *ir, const uint32_t *durations_us,
                          size_t count) {
  (void)ir;
  (void)durations_us;
  (void)count;
  return false;
}
