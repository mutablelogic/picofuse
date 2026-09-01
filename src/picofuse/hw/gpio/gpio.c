#include "private.h"
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief GPIO logical pin structure (host platforms).
 *
 * Deliberately private to this file - a backend never sees this layout,
 * only the `ops`/`context` pair it handed to `_hw_gpio_construct()` (see
 * `_hw_gpio_context()`).
 */
struct hw_gpio_t {
  const hw_gpio_ops_t *ops;
  void *context;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_gpio_callback_t _hw_gpio_callback_func;
static void *_hw_gpio_userdata;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief A handle only counts as usable once init has actually bound it to
 * a backend - ops/context are set together, by whichever hw_gpio_init()
 * built it, never independently. */
static inline bool _hw_gpio_valid(const hw_gpio_t *gpio) {
  return gpio != NULL && gpio->ops != NULL && gpio->context != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS (see private.h)

hw_gpio_t *_hw_gpio_construct(const hw_gpio_ops_t *ops, void *context) {
  if (ops == NULL) {
    return NULL;
  }

  hw_gpio_t *gpio = sys_malloc(sizeof(hw_gpio_t));
  if (gpio == NULL) {
    return NULL;
  }

  gpio->ops = ops;
  gpio->context = context;
  return gpio;
}

void *_hw_gpio_context(const hw_gpio_t *gpio) {
  return gpio != NULL ? gpio->context : NULL;
}

void _hw_gpio_dispatch_callback(uint8_t bank, uint8_t pin,
                                hw_gpio_event_t event) {
  if (_hw_gpio_callback_func != NULL) {
    _hw_gpio_callback_func(bank, pin, event, _hw_gpio_userdata);
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//
// hw_gpio_init()/hw_gpio_count() are platform-specific (see
// darwin/gpio.c, linux/gpio.c) - they're the ones that decide which
// backend (native GPIO, FTDI, ...) a given bank/pin uses, and build the
// hw_gpio_ops_t/context pair via _hw_gpio_construct(). Everything below
// just dispatches through whatever hw_gpio_init() handed back, so it works
// unmodified once a real backend exists - it's shared by both host
// platforms rather than duplicated per-platform.

void hw_gpio_deinit(hw_gpio_t *gpio) {
  if (!_hw_gpio_valid(gpio) || gpio->ops->deinit == NULL) {
    return;
  }
  gpio->ops->deinit(gpio);
  sys_free(gpio);
}

uint8_t hw_gpio_pin(const hw_gpio_t *gpio) {
  if (!_hw_gpio_valid(gpio) || gpio->ops->pin == NULL) {
    return 0;
  }
  return gpio->ops->pin((hw_gpio_t *)gpio);
}

uint8_t hw_gpio_bank(const hw_gpio_t *gpio) {
  if (!_hw_gpio_valid(gpio) || gpio->ops->bank == NULL) {
    return 0;
  }
  return gpio->ops->bank((hw_gpio_t *)gpio);
}

void hw_gpio_set_callback(hw_gpio_callback_t callback, void *userdata) {
  _hw_gpio_callback_func = callback;
  _hw_gpio_userdata = userdata;
}

hw_gpio_mode_t hw_gpio_get_mode(const hw_gpio_t *gpio) {
  if (!_hw_gpio_valid(gpio) || gpio->ops->get_mode == NULL) {
    return hw_gpio_none;
  }
  return gpio->ops->get_mode((hw_gpio_t *)gpio);
}

void hw_gpio_set_mode(hw_gpio_t *gpio, hw_gpio_mode_t mode) {
  if (!_hw_gpio_valid(gpio) || gpio->ops->set_mode == NULL) {
    return;
  }
  gpio->ops->set_mode(gpio, mode);
}

bool hw_gpio_get(const hw_gpio_t *gpio) {
  if (!_hw_gpio_valid(gpio) || gpio->ops->get == NULL) {
    return false;
  }
  return gpio->ops->get((hw_gpio_t *)gpio);
}

void hw_gpio_set(hw_gpio_t *gpio, bool value) {
  if (!_hw_gpio_valid(gpio) || gpio->ops->set == NULL) {
    return;
  }
  gpio->ops->set(gpio, value);
}
