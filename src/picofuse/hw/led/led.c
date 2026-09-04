#include "led.h"
#include <picofuse/sys.h>
#include <stddef.h>
#include <string.h>

#ifdef SYSTEM_NAME_PICO
#include "../../sys/pico/sync.h"
#define _HW_LED_LOCK() _sys_sync_pool_lock()
#define _HW_LED_UNLOCK() _sys_sync_pool_unlock()
#else
#define _HW_LED_LOCK()
#define _HW_LED_UNLOCK()
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief LED handle (shared across all backends).
 *
 * Deliberately private to this file - a backend never sees this layout,
 * only the `ops` it handed to `_hw_led_alloc()` and the embedded `context`
 * scratch buffer it gets back via `_hw_led_context()`.
 */
struct hw_led_t {
  const hw_led_ops_t *ops;
  _Alignas(max_align_t) uint8_t context[HW_LED_CONTEXT_SIZE];
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_led_t _hw_led_pool[HW_LED_POOL_CAPACITY] = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static inline bool _hw_led_valid(const hw_led_t *led) {
  return led != NULL && led->ops != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS (see led.h)

/** @brief Claims a free slot from the static instance pool, or NULL if
 * every slot is already in use. */
static inline hw_led_t *_hw_led_pool_claim(void) {
  for (size_t i = 0; i < HW_LED_POOL_CAPACITY; i++) {
    if (_hw_led_pool[i].ops == NULL) {
      return &_hw_led_pool[i];
    }
  }
  return NULL;
}

hw_led_t *_hw_led_alloc(const hw_led_ops_t *ops) {
  if (ops == NULL) {
    return NULL;
  }

  _HW_LED_LOCK();
  hw_led_t *led = _hw_led_pool_claim();
  if (led != NULL) {
    memset(led->context, 0, sizeof(led->context));
    led->ops = ops;
  }
  _HW_LED_UNLOCK();
  return led;
}

void *_hw_led_context(const hw_led_t *led) {
  return _hw_led_valid(led) ? (void *)led->context : NULL;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

void hw_led_deinit(hw_led_t *led) {
  if (!_hw_led_valid(led)) {
    return;
  }
  // Leave the LED off rather than however it happened to be left, before
  // a backend releases whatever hardware was driving it.
  if (led->ops->clear != NULL) {
    led->ops->clear(led);
  }
  if (led->ops->deinit != NULL) {
    led->ops->deinit(led);
  }
  // Release the pool slot back
  _HW_LED_LOCK();
  led->ops = NULL;
  _HW_LED_UNLOCK();
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_led_set(hw_led_t *led, uint8_t index, bool enabled) {
  if (!_hw_led_valid(led) || led->ops->set == NULL) {
    return false;
  }
  return led->ops->set(led, index, enabled);
}

bool hw_led_set_brightness(hw_led_t *led, uint8_t index, float percent) {
  if (!_hw_led_valid(led) || led->ops->set_brightness == NULL) {
    return false;
  }
  return led->ops->set_brightness(led, index, percent);
}

bool hw_led_clear(hw_led_t *led) {
  if (!_hw_led_valid(led) || led->ops->clear == NULL) {
    return false;
  }
  return led->ops->clear(led);
}

/** Stub implementation: blinking is not implemented yet. */
bool hw_led_blink(hw_led_t *led, uint8_t index, uint32_t period_ms,
                  bool repeating) {
  (void)led;
  (void)index;
  (void)period_ms;
  (void)repeating;
  return false;
}
