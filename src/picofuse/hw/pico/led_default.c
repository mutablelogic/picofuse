#include "../led/led.h"
#include <picofuse/sys.h>

// Needed for the board-config macros (PICO_DEFAULT_LED_PIN,
// CYW43_WL_GPIO_LED_PIN, PICO_DEFAULT_WS2812_PIN, ...) checked below -
// picofuse/hw.h is platform-agnostic and pulls in none of the Pico SDK's
// own headers, so without this every #if defined() here silently sees an
// undefined macro regardless of what the board actually provides.
#include <pico.h>

// Precedence matches the Pico SDK's own board-config convention: a board
// that defines PICO_DEFAULT_LED_PIN takes priority over the CYW43/WS2812
// fallbacks (most non-W boards only ever define one of these anyway), and
// PIMORONI_PRESTO is checked first since that board's "default LED" pin
// is actually driven as a NeoPixel, not a plain GPIO.
uint8_t hw_led_gpio_default(hw_led_type_t *out_type, uint8_t *out_count) {
#if defined(PIMORONI_PRESTO) && defined(PICO_DEFAULT_LED_PIN)
  if (out_type != NULL) {
    *out_type = hw_led_type_neopixel;
  }
  if (out_count != NULL) {
#ifdef PICO_DEFAULT_WS2812_NUM_PIXELS
    *out_count = (uint8_t)PICO_DEFAULT_WS2812_NUM_PIXELS;
#else
    *out_count = 1;
#endif
  }
  return (uint8_t)PICO_DEFAULT_LED_PIN;
#elif defined(PICO_DEFAULT_LED_PIN)
  if (out_type != NULL) {
    *out_type = hw_led_type_gpio;
  }
  if (out_count != NULL) {
    *out_count = 1;
  }
  return (uint8_t)PICO_DEFAULT_LED_PIN;
#elif defined(CYW43_WL_GPIO_LED_PIN)
  if (out_type != NULL) {
    *out_type = hw_led_type_wifi;
  }
  if (out_count != NULL) {
    *out_count = 1;
  }
  return (uint8_t)CYW43_WL_GPIO_LED_PIN;
#elif defined(PICO_DEFAULT_WS2812_PIN)
  if (out_type != NULL) {
    *out_type = hw_led_type_neopixel;
  }
  if (out_count != NULL) {
#ifdef PICO_DEFAULT_WS2812_NUM_PIXELS
    *out_count = (uint8_t)PICO_DEFAULT_WS2812_NUM_PIXELS;
#else
    *out_count = 1;
#endif
  }
  return (uint8_t)PICO_DEFAULT_WS2812_PIN;
#else
  if (out_type != NULL) {
    *out_type = hw_led_type_none;
  }
  if (out_count != NULL) {
    *out_count = 0;
  }
  return HW_LED_GPIO_NONE;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// TYPES

// hw_led_init_default() constructs its own hw_gpio_t/hw_pwm_t (unlike
// hw_led_init_gpio()/hw_led_init_pwm(), which only ever borrow a
// caller-supplied one) and must release them again on deinit. Rather than
// teach led_gpio.c/led_pwm.c about an ownership flag they otherwise have
// no use for, this wraps whichever backend handle hw_led_gpio_default()
// resolved to in a second hw_led_t: set/clear forward to it, and deinit
// tears down the wrapped handle plus whatever this function allocated
// underneath it. Wi-Fi is the exception - hw_led_init_wifi() never touches
// a real GPIO/PWM, so gpio/pwm stay NULL for that type.
typedef struct {
  hw_led_t *inner;
  hw_gpio_t *gpio;
  hw_pwm_t *pwm;
} _hw_led_default_ctx_t;

_Static_assert(sizeof(_hw_led_default_ctx_t) <= HW_LED_CONTEXT_SIZE,
              "_hw_led_default_ctx_t exceeds HW_LED_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool _hw_led_default_set(hw_led_t *led, uint8_t index, bool enabled) {
  _hw_led_default_ctx_t *ctx = _hw_led_context(led);
  return hw_led_set(ctx->inner, index, enabled);
}

static bool _hw_led_default_set_brightness(hw_led_t *led, uint8_t index,
                                           float percent) {
  _hw_led_default_ctx_t *ctx = _hw_led_context(led);
  return hw_led_set_brightness(ctx->inner, index, percent);
}

static bool _hw_led_default_clear(hw_led_t *led) {
  _hw_led_default_ctx_t *ctx = _hw_led_context(led);
  return hw_led_clear(ctx->inner);
}

static void _hw_led_default_deinit(hw_led_t *led) {
  _hw_led_default_ctx_t *ctx = _hw_led_context(led);
  hw_led_deinit(ctx->inner);
  if (ctx->pwm != NULL) {
    hw_pwm_deinit(ctx->pwm);
  }
  if (ctx->gpio != NULL) {
    hw_gpio_deinit(ctx->gpio);
  }
}

static const hw_led_ops_t _hw_led_default_ops = {
    .set = _hw_led_default_set,
    .set_brightness = _hw_led_default_set_brightness,
    .clear = _hw_led_default_clear,
    .deinit = _hw_led_default_deinit,
};

// Allocates the wrapper handle above and populates it - on failure (pool
// exhaustion), tears down everything the caller already built instead of
// leaking it, same as hw_led_init_default() does for its own earlier
// failure paths below.
static hw_led_t *_hw_led_default_wrap(hw_led_t *inner, hw_gpio_t *gpio,
                                      hw_pwm_t *pwm) {
  hw_led_t *led = _hw_led_alloc(&_hw_led_default_ops);
  if (led == NULL) {
    hw_led_deinit(inner);
    if (pwm != NULL) {
      hw_pwm_deinit(pwm);
    }
    if (gpio != NULL) {
      hw_gpio_deinit(gpio);
    }
    return NULL;
  }

  _hw_led_default_ctx_t *ctx = _hw_led_context(led);
  ctx->inner = inner;
  ctx->gpio = gpio;
  ctx->pwm = pwm;
  return led;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_led_t *hw_led_init_default(void) {
  hw_led_type_t led_type = hw_led_type_none;
  uint8_t led_count = 0;
  uint8_t led_pin = hw_led_gpio_default(&led_type, &led_count);

  sys_debugf("hw", "led_init_default: type=%u pin=%u count=%u", led_type,
             led_pin, led_count);

  if (led_type == hw_led_type_none || led_pin == HW_LED_GPIO_NONE) {
    return NULL;
  }

  // Wi-Fi is a CYW43 GPIO, not a real RP2040 pin - nothing to construct
  // or own here beyond what hw_led_init_wifi() itself already handles.
  if (led_type == hw_led_type_wifi) {
    hw_led_t *inner = hw_led_init_wifi();
    if (inner == NULL) {
      return NULL;
    }
    return _hw_led_default_wrap(inner, NULL, NULL);
  }

  hw_gpio_mode_t mode = hw_gpio_output;
  if (led_type == hw_led_type_pwm) {
    mode = hw_gpio_pwm;
  }

  hw_gpio_t *gpio = hw_gpio_init(0, led_pin, mode);
  if (gpio == NULL) {
    return NULL;
  }

  hw_led_t *inner = NULL;
  hw_pwm_t *pwm = NULL;
  switch (led_type) {
  case hw_led_type_gpio:
    inner = hw_led_init_gpio(gpio);
    break;
  case hw_led_type_neopixel:
    inner = hw_led_init_neopixel(gpio, led_count > 0 ? led_count : 1);
    break;
  case hw_led_type_pwm: {
    hw_pwm_config_t config = {
        .period_ns = 1000000u,
        .duty_percent = 0.0f,
        .enabled = false,
    };
    pwm = hw_pwm_init(gpio, NULL, NULL, &config);
    if (pwm == NULL) {
      hw_gpio_deinit(gpio);
      return NULL;
    }
    inner = hw_led_init_pwm(pwm);
    break;
  }
  default:
    hw_gpio_deinit(gpio);
    return NULL;
  }

  if (inner == NULL) {
    if (pwm != NULL) {
      hw_pwm_deinit(pwm);
    }
    hw_gpio_deinit(gpio);
    return NULL;
  }

  return _hw_led_default_wrap(inner, gpio, pwm);
}
