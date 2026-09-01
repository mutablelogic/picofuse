#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_adc_* lifecycle and basic reads: init a GPIO-backed channel and the
// internal temperature sensor, round-trip channel<->pin mapping, read raw
// 12-bit samples at 0/1/many num_samples (all landing in the valid
// 0-4095 range regardless of what's actually wired to the pin), and check
// the temperature sensor reports something plausible for a board sitting
// at room temperature (or lightly self-heated by its own MCU). If the
// platform has no ADC backend at all (host stubs), hw_adc_count() reports
// 0 and there's nothing to exercise.
test_main_hw(0) {
  uint8_t count = hw_adc_count();
  sys_debugf("hw_003", "hw_adc_count() = %u", count);
  if (count == 0) {
    return;
  }

  // Channel 0's GPIO pin <-> channel round trip.
  uint8_t pin = hw_adc_gpio_pin(0);
  test_assert(pin != UINT8_MAX);
  test_assert(hw_adc_gpio_channel(pin) == 0);

  // The internal temperature channel, one past the external ones
  // hw_adc_count() reports, has no GPIO mapping.
  test_assert(hw_adc_gpio_pin(count) == UINT8_MAX);

  // Init a GPIO-backed channel.
  hw_gpio_t *gpio = hw_gpio_init(0, pin, hw_gpio_none);
  test_assert(gpio != NULL);

  hw_adc_t *adc = hw_adc_init_pin(gpio);
  test_assert(adc != NULL);

  // A single, immediate reading - num_samples 0 and 1 are equivalent.
  uint16_t raw0 = hw_adc_read_12(adc, 0);
  test_assert(raw0 <= 4095);
  uint16_t raw1 = hw_adc_read_12(adc, 1);
  test_assert(raw1 <= 4095);

  // Averaging over many samples still lands in the same valid range.
  uint16_t raw_avg = hw_adc_read_12(adc, 64);
  test_assert(raw_avg <= 4095);

  hw_adc_deinit(adc);
  hw_gpio_deinit(gpio);

  // Internal temperature sensor.
  hw_adc_t *temp = hw_adc_init_temperature();
  test_assert(temp != NULL);

  float celsius = hw_adc_read_temperature(temp, 64);
  sys_debugf("hw_003", "hw_adc_read_temperature() = %f", (double)celsius);
  test_assert(celsius >= 10.0f && celsius <= 50.0f);

  hw_adc_deinit(temp);
}
