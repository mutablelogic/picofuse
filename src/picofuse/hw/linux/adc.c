#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Linux has no ADC channels picofuse knows about, but its kernel reports
// temperatures in sysfs: a thermal zone, in millidegrees Celsius, such as a
// Raspberry Pi's CPU sensor, or else a hwmon sensor's temp1_input, in the same
// units. The internal temperature sensor is the first of those which can be read.

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_adc_t {
  char path[64]; ///< The sysfs file the temperature is read from
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_adc_t _hw_adc_temperature = {0};

static const char *const _hw_adc_temperature_paths[] = {
    "/sys/class/thermal/thermal_zone0/temp",
    "/sys/class/hwmon/hwmon0/temp1_input",
    "/sys/class/hwmon/hwmon1/temp1_input",
    NULL,
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

// Reads millidegrees Celsius from a sysfs file
static bool _hw_adc_read_millidegrees(const char *path, long *millidegrees) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    return false;
  }
  bool ok = fscanf(file, "%ld", millidegrees) == 1;
  fclose(file);
  return ok;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** No ADC channels on Linux. */
hw_adc_t *hw_adc_init_pin(hw_gpio_t *gpio) {
  (void)gpio;
  return NULL;
}

/** The first temperature in sysfs which can be read, or NULL if there's none. */
hw_adc_t *hw_adc_init_temperature(void) {
  for (size_t i = 0; _hw_adc_temperature_paths[i] != NULL; i++) {
    long millidegrees;
    if (_hw_adc_read_millidegrees(_hw_adc_temperature_paths[i], &millidegrees)) {
      sys_sprintf(_hw_adc_temperature.path, sizeof(_hw_adc_temperature.path), "%s",
                  _hw_adc_temperature_paths[i]);
      return &_hw_adc_temperature;
    }
  }
  return NULL;
}

/** Nothing to release: the temperature is read from sysfs each time. */
void hw_adc_deinit(hw_adc_t *adc) { (void)adc; }

/** No ADC channels on Linux. */
uint8_t hw_adc_count(void) { return 0; }

/** No pin is ADC-capable on Linux. */
uint8_t hw_adc_gpio_channel(uint8_t pin) {
  (void)pin;
  return UINT8_MAX;
}

/** No channel is GPIO-mapped on Linux. */
uint8_t hw_adc_gpio_pin(uint8_t channel) {
  (void)channel;
  return UINT8_MAX;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** No ADC channels on Linux. */
uint16_t hw_adc_read_12(hw_adc_t *adc, uint16_t num_samples) {
  (void)adc;
  (void)num_samples;
  return 0;
}

/** No ADC channels on Linux. */
uint16_t hw_adc_read_16(hw_adc_t *adc, uint16_t num_samples) {
  (void)adc;
  (void)num_samples;
  return 0;
}

/** No ADC channels on Linux. */
float hw_adc_read_voltage(hw_adc_t *adc, uint16_t num_samples) {
  (void)adc;
  (void)num_samples;
  return 0.0f;
}

/** The temperature in degrees Celsius, averaged over num_samples reads, or 0. */
float hw_adc_read_temperature(hw_adc_t *adc, uint16_t num_samples) {
  if (adc == NULL || adc->path[0] == '\0') {
    return 0.0f;
  }
  uint16_t samples = num_samples == 0 ? 1 : num_samples;
  long total = 0;
  uint16_t read = 0;
  for (uint16_t i = 0; i < samples; i++) {
    long millidegrees;
    if (_hw_adc_read_millidegrees(adc->path, &millidegrees)) {
      total += millidegrees;
      read++;
    }
  }
  return read == 0 ? 0.0f : (float)total / (float)read / 1000.0f;
}

/** No DMA-driven ADC reads on Linux. */
bool hw_adc_read_dma(hw_adc_t *adc, uint16_t *buf, size_t samples,
                     size_t partitions, uint32_t freq,
                     hw_adc_dma_callback_t callback, void *userdata) {
  (void)adc;
  (void)buf;
  (void)samples;
  (void)partitions;
  (void)freq;
  (void)callback;
  (void)userdata;
  return false;
}
