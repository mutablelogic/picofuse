#include "../../sys/pico/sync.h"
#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_pwm_t {
  uint8_t slice;
  uint8_t channel;
  bool initialized;
  hw_pwm_callback_t callback;
  void *userdata;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// One slot per (slice, channel) pair
static hw_pwm_t _hw_pwm_pool[NUM_PWM_SLICES * 2] = {0};

// Guards a single, process-wide install of the PWM_IRQ_WRAP vector
static bool _hw_pwm_irq_installed = false;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static void _hw_pwm_callback_handler(void);

static inline uint8_t _hw_pwm_handle_index(uint8_t slice, uint8_t channel) {
  return (uint8_t)(slice * 2u + channel);
}

/** @brief True if `pwm` was actually handed out by hw_pwm_init*() and not
 * yet released by hw_pwm_deinit(). */
static inline bool _hw_pwm_valid(const hw_pwm_t *pwm) {
  return pwm != NULL && pwm->slice < NUM_PWM_SLICES && pwm->channel < 2 &&
         pwm->initialized;
}

static inline uint16_t _hw_pwm_get_wrap(uint8_t slice) {
  return pwm_hw->slice[slice].top;
}

static inline float _hw_pwm_get_divider(uint8_t slice) {
  return (float)pwm_hw->slice[slice].div / 16.0f; // 16.16 fixed point
}

static inline uint16_t _hw_pwm_get_level(uint8_t slice, uint8_t channel) {
  return channel == 0 ? pwm_hw->slice[slice].cc & 0xFFFFu
                      : (pwm_hw->slice[slice].cc >> 16) & 0xFFFFu;
}

static inline bool _hw_pwm_get_running(uint8_t slice) {
  return (pwm_hw->slice[slice].csr & PWM_CH0_CSR_EN_BITS) != 0;
}

/** @brief Duty percentage -> raw counter-compare level for a given wrap -
 * shared by hw_pwm_init() and hw_pwm_set_duty_percent() rather than
 * duplicated between them. */
static inline uint16_t _hw_pwm_duty_to_level(float duty_percent,
                                             uint16_t wrap) {
  if (duty_percent <= 0.0f) {
    return 0;
  }
  if (duty_percent >= 100.0f) {
    return wrap;
  }
  uint64_t scaled = (uint64_t)(wrap + 1) * (uint64_t)(duty_percent * 10000.0f);
  return (uint16_t)(scaled / (100u * 10000u));
}

/** @brief Nearest (wrap, divider) pair for a target period - searches every
 * raw divider (16.4 fixed point, so 16 == divider 1.0) for the one whose
 * resulting counts-per-period is closest to the target, since the wrap
 * counter alone (max 65536 counts) can't reach most real-world periods at
 * divider 1.0. */
static inline float _hw_pwm_period_ns_to_divider(uint64_t period_ns,
                                                 uint16_t *out_wrap) {
  uint32_t sys_clk = clock_get_hz(clk_sys);
  long double target_counts =
      ((long double)period_ns * (long double)sys_clk) / 1000000000.0L;
  if (target_counts < 1.0L) {
    target_counts = 1.0L;
  }

  uint16_t best_wrap = 0;
  uint16_t best_div_raw = 16;
  long double best_error = target_counts;

  for (uint16_t div_raw = 16; div_raw < 4096; div_raw++) {
    long double counts_f = target_counts * 16.0L / (long double)div_raw;
    uint32_t counts = (uint32_t)(counts_f + 0.5L);
    if (counts < 1) {
      counts = 1;
    }
    if (counts > 65536) {
      counts = 65536;
    }

    long double actual_counts =
        ((long double)counts * (long double)div_raw) / 16.0L;
    long double error = actual_counts > target_counts
                            ? actual_counts - target_counts
                            : target_counts - actual_counts;
    if (error < best_error) {
      best_error = error;
      best_wrap = (uint16_t)(counts - 1);
      best_div_raw = div_raw;
      if (error == 0.0L) {
        break;
      }
    }
  }

  *out_wrap = best_wrap;
  return (float)best_div_raw / 16.0f;
}

static inline uint64_t _hw_pwm_divider_wrap_to_period_ns(float divider,
                                                         uint16_t wrap) {
  uint32_t sys_clk = clock_get_hz(clk_sys);
  float freq = (float)sys_clk / ((float)(wrap + 1) * divider);
  return (uint64_t)(1e9f / freq + 0.5f);
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Attaches (or clears) `pwm`'s wrap callback, installing the shared
 * PWM_IRQ_WRAP vector on first use process-wide. Must be called with
 * _sys_sync_pool_lock() held. */
static void _hw_pwm_attach_callback(hw_pwm_t *pwm, hw_pwm_callback_t callback,
                                    void *userdata) {
  pwm->callback = callback;
  pwm->userdata = userdata;

  if (callback == NULL) {
    pwm_set_irq_enabled(pwm->slice, false);
    return;
  }

  if (!_hw_pwm_irq_installed) {
    _hw_pwm_irq_installed = true;
    irq_set_exclusive_handler(PWM_IRQ_WRAP, _hw_pwm_callback_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);
  }

  // The slice has already been running since pwm_init(..., enabled=true)
  // above, so its raw wrap-pending bit may already be latched by the time
  // we get here - servicing it as a real event would fire the callback
  // immediately (repeatedly, until real time catches up to it) instead of
  // only once the *next* genuine wrap occurs. Clear it first so enabling
  // starts clean.
  pwm_clear_irq(pwm->slice);
  pwm_set_irq_enabled(pwm->slice, true);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_pwm_t *hw_pwm_init(hw_gpio_t *gpio, hw_pwm_callback_t callback,
                      void *userdata, const hw_pwm_config_t *config) {
  sys_debugf("hw", "pwm_init: gpio=%p callback=%p config=%p", gpio, callback,
             config);
  if (gpio == NULL || (callback != NULL && !hw_pwm_irq_supported())) {
    return NULL;
  }

  hw_gpio_set_mode(gpio, hw_gpio_pwm);
  uint8_t pin = hw_gpio_pin(gpio);
  uint8_t slice = pwm_gpio_to_slice_num(pin);
  uint8_t channel = pwm_gpio_to_channel(pin);

  hw_pwm_config_t defaults = {
      .period_ns = _hw_pwm_divider_wrap_to_period_ns(1.0f, 0xFFFF),
      .duty_percent = 0.0f,
      .enabled = false,
  };
  if (config == NULL) {
    config = &defaults;
  }

  uint16_t wrap = 0xFFFF;
  float divider = _hw_pwm_period_ns_to_divider(config->period_ns, &wrap);
  uint16_t level = _hw_pwm_duty_to_level(config->duty_percent, wrap);

  pwm_config pico_config = pwm_get_default_config();
  pwm_config_set_wrap(&pico_config, wrap);
  pwm_config_set_clkdiv(&pico_config, divider);
  pwm_config_set_clkdiv_mode(&pico_config, PWM_DIV_FREE_RUNNING);

  _sys_sync_pool_lock();

  uint8_t index = _hw_pwm_handle_index(slice, channel);
  if (_hw_pwm_pool[index].initialized) {
    _sys_sync_pool_unlock();
    return NULL; // already in use
  }

  hw_pwm_t *pwm = &_hw_pwm_pool[index];
  pwm->slice = slice;
  pwm->channel = channel;
  pwm->initialized = true;

  pwm_init(slice, &pico_config, config->enabled);
  pwm_set_chan_level(slice, channel, level);
  _hw_pwm_attach_callback(pwm, callback, userdata);

  _sys_sync_pool_unlock();

  sys_debugf(
      "hw",
      "pwm_init: pin=%u slice=%u channel=%u period_ns=%lu duty_percent=%f "
      "enabled=%u",
      pin, slice, channel, config->period_ns, (double)config->duty_percent,
      config->enabled);

  return pwm;
}

hw_pwm_t *hw_pwm_init_device(const char *device, uint8_t channel,
                             const hw_pwm_config_t *config) {
  sys_debugf("hw",
      "pwm_init_device: unsupported on this target (device=%s channel=%u)",
      device != NULL ? device : "(null)", channel);
  (void)device;
  (void)channel;
  (void)config;
  return NULL;
}

void hw_pwm_deinit(hw_pwm_t *pwm) {
  sys_debugf("hw", "pwm_deinit: pwm=%p", pwm);
  if (!_hw_pwm_valid(pwm)) {
    return;
  }

  pwm_set_enabled(pwm->slice, false);
  pwm_set_irq_enabled(pwm->slice, false);

  _sys_sync_pool_lock();
  pwm->callback = NULL;
  pwm->userdata = NULL;
  pwm->initialized = false;
  _sys_sync_pool_unlock();
}

///////////////////////////////////////////////////////////////////////////////
// CONFIGURATION

bool hw_pwm_set_period_ns(hw_pwm_t *pwm, uint64_t period_ns) {
  if (!_hw_pwm_valid(pwm)) {
    return false;
  }

  uint16_t wrap;
  float divider = _hw_pwm_period_ns_to_divider(period_ns, &wrap);

  pwm_config pico_config = pwm_get_default_config();
  pwm_config_set_wrap(&pico_config, wrap);
  pwm_config_set_clkdiv(&pico_config, divider);
  pwm_config_set_clkdiv_mode(&pico_config, PWM_DIV_FREE_RUNNING);
  pwm_init(pwm->slice, &pico_config, _hw_pwm_get_running(pwm->slice));

  return true;
}

uint64_t hw_pwm_get_period_ns(const hw_pwm_t *pwm) {
  if (!_hw_pwm_valid(pwm)) {
    return 0;
  }
  return _hw_pwm_divider_wrap_to_period_ns(_hw_pwm_get_divider(pwm->slice),
                                           _hw_pwm_get_wrap(pwm->slice));
}

bool hw_pwm_set_duty_percent(hw_pwm_t *pwm, float duty_percent) {
  if (!_hw_pwm_valid(pwm)) {
    return false;
  }

  uint16_t wrap = _hw_pwm_get_wrap(pwm->slice);
  pwm_set_chan_level(pwm->slice, pwm->channel,
                     _hw_pwm_duty_to_level(duty_percent, wrap));
  return true;
}

float hw_pwm_get_duty_percent(const hw_pwm_t *pwm) {
  if (!_hw_pwm_valid(pwm)) {
    return 0.0f;
  }
  uint16_t wrap = _hw_pwm_get_wrap(pwm->slice);
  uint16_t level = _hw_pwm_get_level(pwm->slice, pwm->channel);
  return (float)level / (float)(wrap + 1) * 100.0f;
}

bool hw_pwm_set_config(hw_pwm_t *pwm, const hw_pwm_config_t *config) {
  if (config == NULL || !_hw_pwm_valid(pwm)) {
    return false;
  }

  if (!hw_pwm_set_period_ns(pwm, config->period_ns) ||
      !hw_pwm_set_duty_percent(pwm, config->duty_percent)) {
    return false;
  }
  hw_pwm_set_enabled(pwm, config->enabled);
  return true;
}

bool hw_pwm_get_config(const hw_pwm_t *pwm, hw_pwm_config_t *out_config) {
  if (!_hw_pwm_valid(pwm) || out_config == NULL) {
    return false;
  }

  out_config->period_ns = hw_pwm_get_period_ns(pwm);
  out_config->duty_percent = hw_pwm_get_duty_percent(pwm);
  out_config->enabled = _hw_pwm_get_running(pwm->slice);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// CONTROL

void hw_pwm_set_enabled(hw_pwm_t *pwm, bool enabled) {
  if (!_hw_pwm_valid(pwm)) {
    return;
  }
  pwm_set_enabled(pwm->slice, enabled);
}

bool hw_pwm_get_enabled(const hw_pwm_t *pwm) {
  if (!_hw_pwm_valid(pwm)) {
    return false;
  }
  return _hw_pwm_get_running(pwm->slice);
}

///////////////////////////////////////////////////////////////////////////////
// INTERRUPTS

bool hw_pwm_irq_supported(void) { return true; }

// Shared by every slice with a callback attached - demuxes which slice(s)
// wrapped via pwm_get_irq_status_mask() and dispatches each of their (up
// to two) channels' callbacks. Snapshots callback/userdata under
// _sys_sync_pool_lock() but calls out to user code outside it - a
// callback that itself touches any picofuse sync primitive sharing this
// same critical section (mutex, cond, waitgroup, ...) would otherwise
// deadlock.
static void _hw_pwm_callback_handler(void) {
  uint32_t status = pwm_get_irq_status_mask();

  for (uint8_t slice = 0; slice < NUM_PWM_SLICES; slice++) {
    if ((status & (1u << slice)) == 0) {
      continue;
    }
    pwm_clear_irq(slice);

    for (uint8_t channel = 0; channel < 2; channel++) {
      hw_pwm_t *pwm = &_hw_pwm_pool[_hw_pwm_handle_index(slice, channel)];

      _sys_sync_pool_lock();
      hw_pwm_callback_t callback = pwm->initialized ? pwm->callback : NULL;
      void *userdata = pwm->userdata;
      _sys_sync_pool_unlock();

      if (callback != NULL) {
        callback(pwm, userdata);
      }
    }
  }
}
