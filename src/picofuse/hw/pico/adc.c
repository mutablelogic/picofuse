#include "dma.h"

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
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

// One past the largest divider adc_set_clkdiv() accepts - its integer field
// is 16 bits wide. adc_set_clkdiv() only checks this via invalid_params_if(),
// which compiles to nothing outside a debug build, so an out-of-range value
// would otherwise reach a float-to-uint32_t cast of a value that overflows
// it - checking it ourselves here keeps that well-defined on every build.
#define ADC_CLKDIV_MAX 65536.0f

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_adc_t {
  uint8_t channel;
  hw_gpio_t *gpio;
  bool temperature;
  hw_dma_fifo_t *dma; // non-NULL while a hw_adc_read_dma() capture is running
  hw_adc_dma_callback_t dma_callback;
  void *dma_userdata;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_adc_t _hw_adc_channels[NUM_ADC_CHANNELS] = {0};

// Tracks whether some hw_adc_t handle has an active hw_adc_read_dma()
// capture - the ADC's mux/FIFO/clkdiv/run bit are single shared hardware,
// not one per channel, so two handles capturing at once would silently
// corrupt each other's stream rather than error out (see
// _hw_adc_dma_claim()). A plain per-handle check (adc->dma != NULL) can't
// catch that, since it only looks at the one handle being called on.
static sys_atomic_t _hw_adc_dma_active = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Validate the ADC handle. Checked against NUM_ADC_CHANNELS, not
 * hw_adc_count() - the latter now reports only external, GPIO-mappable
 * channels, but the internal temperature channel (index
 * ADC_TEMPERATURE_CHANNEL_NUM, one past the external ones) is still a
 * valid channel here; it just has no GPIO of its own (see
 * hw_adc_init_temperature()), so it's the one case allowed through
 * without one - every other channel must have one. */
static inline bool _hw_adc_valid(const hw_adc_t *adc) {
  return adc != NULL && adc->channel < NUM_ADC_CHANNELS &&
         (adc->gpio != NULL || adc->temperature);
}

/**
 * @brief Claims _hw_adc_dma_active for a new hw_adc_read_dma() capture,
 * or returns false if some other handle already has one running.
 * sys_atomic_inc() only ever lets the caller that takes the counter from
 * 0 to 1 through; anyone else (the counter was already >= 1) backs
 * straight back out, leaving it exactly as they found it.
 */
static bool _hw_adc_dma_claim(void) {
  if (sys_atomic_inc(&_hw_adc_dma_active) != 1) {
    sys_atomic_dec(&_hw_adc_dma_active);
    return false;
  }
  return true;
}

/** @brief Releases a claim taken by _hw_adc_dma_claim() without having
 * started the ADC (an hw_adc_read_dma() failure between claiming and
 * actually calling adc_run(true)) - unlike _hw_adc_dma_stop(), there's
 * nothing running to stop. */
static void _hw_adc_dma_release(void) { sys_atomic_dec(&_hw_adc_dma_active); }

/**
 * @brief Stops ADC free-running, restores the clock divider to its
 * full-speed default, and releases the _hw_adc_dma_claim() claim taken
 * when the capture started. Shared by every path that tears down a
 * hw_adc_read_dma() capture - the capture's own callback returning false
 * (_hw_adc_dma_trampoline), a single-shot read preempting it
 * (_hw_adc_read_raw()), and hw_adc_deinit() - since none of those besides
 * the callback path would otherwise know to undo hw_adc_read_dma()'s
 * adc_set_clkdiv(), leaving a later single-shot read (which never sets its
 * own clkdiv) sampling at whatever rate the capture last configured.
 */
static void _hw_adc_dma_stop(void) {
  adc_run(false);
  adc_fifo_drain();
  adc_set_clkdiv(0);
  _hw_adc_dma_release();
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

uint8_t hw_adc_count(void) { return (uint8_t)(NUM_ADC_CHANNELS - 1); }

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

  if (adc->dma != NULL) {
    hw_dma_fifo_deinit(adc->dma);
    adc->dma = NULL;
    _hw_adc_dma_stop();
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
  if (pin < ADC_BASE_PIN || pin >= (uint8_t)(ADC_BASE_PIN + hw_adc_count())) {
    return UINT8_MAX;
  }

  return (uint8_t)(pin - ADC_BASE_PIN);
}

uint8_t hw_adc_gpio_pin(uint8_t channel) {
  if (channel >= hw_adc_count()) {
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

  // A single-shot/averaged read reconfigures the FIFO and free-run state
  // this handle's hw_adc_read_dma() capture (if any) depends on, so
  // letting that capture keep running underneath would corrupt both.
  // Stop it first rather than leaving the two fighting over the ADC -
  // including the ADC itself, which hw_dma_fifo_deinit() alone doesn't
  // touch: left free-running, it keeps refilling the FIFO forever and
  // the adc_fifo_drain() below would never see it go empty.
  if (adc->dma != NULL) {
    hw_dma_fifo_deinit(adc->dma);
    adc->dma = NULL;
    _hw_adc_dma_stop();
  }

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

/**
 * @brief Bridges hw_dma_fifo_t's generic (buf, samples, userdata) callback
 * back to the caller's hw_adc_dma_callback_t, which additionally wants the
 * hw_adc_t handle. `userdata` here is always the owning hw_adc_t*, passed
 * as such to hw_dma_fifo_init() below.
 */
static bool _hw_adc_dma_trampoline(void *buf, size_t samples, void *userdata) {
  hw_adc_t *adc = userdata;
  bool keep_going =
      adc->dma_callback(adc, (uint16_t *)buf, samples, adc->dma_userdata);
  if (!keep_going) {
    _hw_adc_dma_stop();
    adc->dma = NULL;
  }
  return keep_going;
}

bool hw_adc_read_dma(hw_adc_t *adc, uint16_t *buf, size_t samples,
                     size_t partitions, uint32_t freq,
                     hw_adc_dma_callback_t callback, void *userdata) {
  if (!_hw_adc_valid(adc) || buf == NULL || samples == 0 || partitions < 2 ||
      callback == NULL || adc->dma != NULL) {
    return false;
  }

  // Claim the ADC's shared hardware before touching any of it, so a
  // losing caller (some other handle already capturing) makes zero
  // register writes rather than racing the winner's in-progress setup.
  if (!_hw_adc_dma_claim()) {
    return false;
  }

  // Each conversion takes a minimum of 96 ADC clock cycles, so a period of
  // fewer than that (a `freq` above the ADC's ~500kHz free-running max)
  // can't be represented as a non-negative divider - reject it rather than
  // let adc_set_clkdiv() truncate a negative float into a huge one. A
  // `freq` low enough to overflow the divider's 16-bit integer field the
  // other way is rejected too, for the same reason. clk_adc's rate is
  // queried at runtime rather than assumed fixed, since a board can
  // reconfigure it away from the usual 48MHz default.
  float clkdiv = 0.0f; // 0 => back-to-back conversions, the maximum rate.
  if (freq != 0) {
    float adc_clock_hz = (float)clock_get_hz(clk_adc);
    clkdiv = (adc_clock_hz / (float)freq) - 96.0f;
    if (clkdiv < 0.0f || clkdiv >= ADC_CLKDIV_MAX) {
      _hw_adc_dma_release();
      return false;
    }
  }

  adc_select_input(adc->channel);
  adc_fifo_drain();

  // Push every completed conversion to the FIFO, request DMA as soon as
  // one sample is present, no error flag (we're not shifting to 8 bits),
  // full 12-bit samples (no shift).
  adc_fifo_setup(true, true, 1, false, false);
  adc_set_clkdiv(clkdiv);

  adc->dma_callback = callback;
  adc->dma_userdata = userdata;
  adc->dma = hw_dma_fifo_init(&adc_hw->fifo, DREQ_ADC, hw_dma_fifo_uint16, buf,
                              samples, partitions, _hw_adc_dma_trampoline, adc);
  if (adc->dma == NULL) {
    _hw_adc_dma_release();
    return false;
  }

  hw_dma_fifo_start(adc->dma);
  adc_run(true);
  return true;
}
