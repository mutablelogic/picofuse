#include <picofuse/hw.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no ADC hardware on this platform. */
hw_adc_t *hw_adc_init_pin(hw_gpio_t *gpio) {
  (void)gpio;
  return NULL;
}

/** Stub implementation: no ADC hardware on this platform. */
hw_adc_t *hw_adc_init_temperature(void) { return NULL; }

/** Stub implementation: no ADC hardware on this platform. */
void hw_adc_deinit(hw_adc_t *adc) { (void)adc; }

/** Stub implementation: no ADC channels on this platform. */
uint8_t hw_adc_count(void) { return 0; }

/** Stub implementation: no pin is ever ADC-capable on this platform. */
uint8_t hw_adc_gpio_channel(uint8_t pin) {
  (void)pin;
  return UINT8_MAX;
}

/** Stub implementation: no channel is ever GPIO-mapped on this platform. */
uint8_t hw_adc_gpio_pin(uint8_t channel) {
  (void)channel;
  return UINT8_MAX;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** Stub implementation: no ADC hardware on this platform. */
uint16_t hw_adc_read_12(hw_adc_t *adc, uint16_t num_samples) {
  (void)adc;
  (void)num_samples;
  return 0;
}

/** Stub implementation: no ADC hardware on this platform. */
uint16_t hw_adc_read_16(hw_adc_t *adc, uint16_t num_samples) {
  (void)adc;
  (void)num_samples;
  return 0;
}

/** Stub implementation: no ADC hardware on this platform. */
float hw_adc_read_voltage(hw_adc_t *adc, uint16_t num_samples) {
  (void)adc;
  (void)num_samples;
  return 0.0f;
}

/** Stub implementation: no ADC hardware on this platform. */
float hw_adc_read_temperature(hw_adc_t *adc, uint16_t num_samples) {
  (void)adc;
  (void)num_samples;
  return 0.0f;
}

/** Stub implementation: no DMA-driven ADC reads on this platform. */
bool hw_adc_read_dma(hw_adc_t *adc, uint16_t *buf, size_t count,
                     size_t partitions, hw_adc_dma_callback_t callback,
                     void *userdata) {
  (void)adc;
  (void)buf;
  (void)count;
  (void)partitions;
  (void)callback;
  (void)userdata;
  return false;
}
