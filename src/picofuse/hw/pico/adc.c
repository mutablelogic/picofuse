#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef ADC_VREF
#define ADC_VREF (3.3f)
#endif

// Upper bound on hw_adc_read_12() sample averaging. At the default ~500kSPS
// free-running rate this caps a single call to roughly 2ms of blocking time.
#define ADC_MAX_SAMPLES 1024u

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_adc_t {
  uint8_t channel;
  hw_gpio_t *gpio;
  bool temperature;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_adc_t _hw_adc_channels[NUM_ADC_CHANNELS] = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Validate the ADC handle. The internal temperature channel has no
 * backing GPIO by design (see hw_adc_init_temperature()), so it is valid
 * without one; every other channel must have one. */
static inline bool _hw_adc_valid(const hw_adc_t *adc) {
  return adc != NULL && adc->channel < hw_adc_count() &&
         (adc->gpio != NULL || adc->temperature);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

uint8_t hw_adc_count(void) { return (uint8_t)NUM_ADC_CHANNELS; }

hw_adc_t *hw_adc_init_pin(hw_gpio_t *gpio) {
  sys_debugf("hw", "adc_init_pin: gpio=%p", (void *)gpio);
  if (gpio == NULL) {
    return NULL;
  }

  uint8_t channel = hw_adc_gpio_channel(hw_gpio_pin(gpio));
  if (channel == UINT8_MAX) {
    return NULL;
  }

  hw_gpio_set_mode(gpio, hw_gpio_adc);

  hw_adc_t *adc = &_hw_adc_channels[channel];
  adc->channel = channel;
  adc->gpio = gpio;
  adc->temperature = false;
  return adc;
}

hw_adc_t *hw_adc_init_temperature(void) {
  sys_debugf("hw", "adc_init_temperature");
  adc_set_temp_sensor_enabled(true);

  hw_adc_t *adc = &_hw_adc_channels[ADC_TEMPERATURE_CHANNEL_NUM];
  adc->channel = ADC_TEMPERATURE_CHANNEL_NUM;
  adc->gpio = NULL;
  adc->temperature = true;
  return adc;
}

void hw_adc_deinit(hw_adc_t *adc) {
  sys_debugf("hw", "adc_deinit: adc=%p", (void *)adc);
  if (!_hw_adc_valid(adc)) {
    return;
  }

  if (adc->channel == ADC_TEMPERATURE_CHANNEL_NUM) {
    adc_set_temp_sensor_enabled(false);
  }

  adc->channel = UINT8_MAX;
  adc->gpio = NULL;
  adc->temperature = false;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

uint8_t hw_adc_gpio_channel(uint8_t pin) {
  if (pin < ADC_BASE_PIN ||
      pin >= (uint8_t)(ADC_BASE_PIN + NUM_ADC_CHANNELS - 1)) {
    return UINT8_MAX;
  }

  return (uint8_t)(pin - ADC_BASE_PIN);
}

uint8_t hw_adc_gpio_pin(uint8_t channel) {
  if (channel >= (uint8_t)(NUM_ADC_CHANNELS - 1)) {
    return UINT8_MAX;
  }

  return (uint8_t)(ADC_BASE_PIN + channel);
}

/**
 * @brief Sample the ADC and return the averaged raw 12-bit reading at full
 * precision (not rounded to an integer count). num_samples > 1 averaging
 * only reduces noise in the mean if that fractional precision survives to
 * the caller; hw_adc_read_12() rounds it away for its integer-count
 * contract, so callers that need the benefit of averaging beyond 1 LSB
 * (see hw_adc_read_voltage()) use this directly instead of routing through
 * hw_adc_read_12().
 */
static float _hw_adc_read_raw(hw_adc_t *adc, uint16_t num_samples) {
  sys_assert(_hw_adc_valid(adc));

  if (adc->temperature) {
    adc_set_temp_sensor_enabled(true);
  }

  adc_select_input(adc->channel);

  // adc_fifo_setup()'s enable bit is sticky across calls (never disabled
  // between reads), so if the FIFO still held any entries from a prior
  // conversion on a *different* channel - e.g. adc_run(false) not fully
  // stopping free-running before the previous adc_fifo_drain() checked
  // is_empty() - they'd otherwise still be sitting here. Discarding two
  // "settling" samples below then assumes those are fresh conversions on
  // the newly-selected channel; if they're actually stale leftovers from
  // the previous channel instead, the settling discard doesn't do its job
  // and the average gets contaminated by a different channel's readings.
  // Drain unconditionally first so every read starts from a clean FIFO.
  adc_fifo_drain();

  float result;
  if (num_samples <= 1) {
    adc_run(false);
    result = (float)adc_read();
  } else {
    if (num_samples > ADC_MAX_SAMPLES) {
      num_samples = ADC_MAX_SAMPLES;
    }

    // Free-run the ADC and average samples pulled from the FIFO.
    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);

    // Discard the first couple of conversions to let the ADC settle.
    (void)adc_fifo_get_blocking();
    (void)adc_fifo_get_blocking();

    uint32_t sum = 0;
    for (uint16_t i = 0; i < num_samples; i++) {
      sum += adc_fifo_get_blocking();
    }

    adc_run(false);
    adc_fifo_drain();

    result = (float)sum / (float)num_samples;
  }

  return result;
}

uint16_t hw_adc_read_12(hw_adc_t *adc, uint16_t num_samples) {
  return (uint16_t)(_hw_adc_read_raw(adc, num_samples) + 0.5f);
}

uint16_t hw_adc_read_16(hw_adc_t *adc, uint16_t num_samples) {
  uint16_t raw12 = hw_adc_read_12(adc, num_samples);
  return (uint16_t)((raw12 << 4) | (raw12 >> 8));
}

float hw_adc_read_voltage(hw_adc_t *adc, uint16_t num_samples) {
  float raw12 = _hw_adc_read_raw(adc, num_samples);
  const float conversion_factor = ADC_VREF / (float)(1 << 12);
  return raw12 * conversion_factor;
}

float hw_adc_read_temperature(hw_adc_t *adc, uint16_t num_samples) {
  sys_assert(_hw_adc_valid(adc));
  sys_assert(adc->channel == ADC_TEMPERATURE_CHANNEL_NUM);

  float voltage = hw_adc_read_voltage(adc, num_samples);
  return 27.0f - ((voltage - 0.706f) / 0.001721f);
}

bool hw_adc_read_dma(hw_adc_t *adc, uint16_t *buf, size_t count,
                     size_t partitions, hw_adc_dma_callback_t callback,
                     void *userdata) {
  // Not implemented yet - no DMA-driven capture on this backend.
  (void)adc;
  (void)buf;
  (void)count;
  (void)partitions;
  (void)callback;
  (void)userdata;
  return false;
}
