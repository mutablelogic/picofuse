#include <picofuse/hw.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no SPI hardware on this platform. */
hw_deviceio_t *hw_spi_init_default(uint32_t baud_rate,
                                   const hw_spi_config_t *config) {
  (void)baud_rate;
  (void)config;
  return NULL;
}

/** Stub implementation: no SPI hardware on this platform. */
hw_deviceio_t *hw_spi_init(uint8_t index, hw_gpio_t *sck_pin,
                           hw_gpio_t *tx_pin, hw_gpio_t *rx_pin,
                           hw_gpio_t *cs_pin, uint32_t baud_rate,
                           const hw_spi_config_t *config) {
  (void)index;
  (void)sck_pin;
  (void)tx_pin;
  (void)rx_pin;
  (void)cs_pin;
  (void)baud_rate;
  (void)config;
  return NULL;
}

/** Stub implementation: no SPI hardware on this platform. */
hw_deviceio_t *hw_spi_init_device(const char *device, uint32_t baud_rate,
                                  const hw_spi_config_t *config) {
  (void)device;
  (void)baud_rate;
  (void)config;
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** Stub implementation: no SPI adapters on this platform. */
uint8_t hw_spi_count(void) { return 0; }
