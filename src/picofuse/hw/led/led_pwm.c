#include "led.h"
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct {
  hw_pwm_t *pwm;
} _hw_led_pwm_ctx_t;

_Static_assert(sizeof(_hw_led_pwm_ctx_t) <= HW_LED_CONTEXT_SIZE,
               "_hw_led_pwm_ctx_t exceeds HW_LED_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool _hw_led_pwm_set(hw_led_t *led, uint8_t index, bool enabled) {
  (void)index; // a single PWM channel has no addressable sub-index
  _hw_led_pwm_ctx_t *ctx = _hw_led_context(led);
  hw_pwm_set_duty_percent(ctx->pwm, enabled ? 100.0f : 0.0f);
  hw_pwm_set_enabled(ctx->pwm, enabled);
  return true;
}

static bool _hw_led_pwm_clear(hw_led_t *led) {
  return _hw_led_pwm_set(led, 0, false);
}

static const hw_led_ops_t _hw_led_pwm_ops = {
    .set = _hw_led_pwm_set,
    .clear = _hw_led_pwm_clear,
    .deinit = NULL,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_led_t *hw_led_init_pwm(hw_pwm_t *pwm) {
  sys_debugf("hw", "led_init_pwm: pwm=%p", (void *)pwm);
  if (pwm == NULL) {
    return NULL;
  }

  // Ensure PWM-backed LEDs always start in an off/disabled state.
  hw_pwm_set_duty_percent(pwm, 0.0f);
  hw_pwm_set_enabled(pwm, false);

  hw_led_t *led = _hw_led_alloc(&_hw_led_pwm_ops);
  if (led == NULL) {
    return NULL;
  }

  _hw_led_pwm_ctx_t *ctx = _hw_led_context(led);
  ctx->pwm = pwm;
  return led;
}
