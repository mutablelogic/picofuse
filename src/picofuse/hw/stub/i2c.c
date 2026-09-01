#include <picofuse/hw.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no I2C hardware on this platform. */
hw_deviceio_t *hw_i2c_init_default(uint8_t addr) {
  (void)addr;
  return NULL;
}

/** Stub implementation: no I2C hardware on this platform. */
hw_deviceio_t *hw_i2c_init(uint8_t index, uint8_t addr, hw_gpio_t *sda_pin,
                           hw_gpio_t *scl_pin) {
  (void)index;
  (void)addr;
  (void)sda_pin;
  (void)scl_pin;
  return NULL;
}

/** Stub implementation: no I2C hardware on this platform. */
hw_deviceio_t *hw_i2c_init_device(const char *device, uint8_t addr) {
  (void)device;
  (void)addr;
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** Stub implementation: no I2C adapters on this platform. */
uint8_t hw_i2c_count(void) { return 0; }

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** Stub implementation: no I2C hardware on this platform. */
bool hw_i2c_detect(hw_deviceio_t *device, uint8_t addr) {
  (void)device;
  (void)addr;
  return false;
}
