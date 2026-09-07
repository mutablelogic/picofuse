#include "private.h"

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

sys_atomic_t _hw_led_active_blinks;

#ifndef SYSTEM_NAME_PICO
pthread_mutex_t _hw_led_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** Callback for the blink timer. Flips the desired state and marks the blink as
 * pending. If the blink is non-repeating and has completed its single flip,
 * stops the timer.
 */
static void _hw_led_blink_timer_cb(sys_timer_t *timer) {
  hw_led_t *led = (hw_led_t *)sys_timer_userdata(timer);
  if (led == NULL) {
    return;
  }

  bool stop = false;
  _HW_LED_LOCK();
  if (_hw_led_valid(led)) {
    led->blink.desired = !led->blink.desired;
    led->blink.flip_pending = true;
    if (!led->blink.repeating) {
      led->blink.timer = NULL;
      stop = true;
    }
  }
  _HW_LED_UNLOCK();

  if (stop) {
    // Safe to call from inside its own callback
    sys_timer_deinit(timer);
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE (module)

void _hw_led_poll(void) {
  // Fast path: skip the pool scan entirely while nothing has an active
  // blink.
  if (sys_atomic_get(&_hw_led_active_blinks) == 0) {
    return;
  }

  for (size_t i = 0; i < HW_LED_POOL_CAPACITY; i++) {
    hw_led_t *led = &_hw_led_pool[i];

    _HW_LED_LOCK();
    bool pending = _hw_led_valid(led) && led->blink.flip_pending;
    bool desired = led->blink.desired;
    uint8_t index = led->blink.index;
    bool finished = pending && led->blink.timer == NULL;
    if (pending) {
      led->blink.flip_pending = false;
    }
    _HW_LED_UNLOCK();

    if (pending && led->ops->set != NULL) {
      led->ops->set(led, index, desired);
    }
    if (finished) {
      sys_atomic_dec(&_hw_led_active_blinks);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_led_blink(hw_led_t *led, uint8_t index, uint32_t period_ms,
                  bool repeating) {
  if (!_hw_led_valid(led) || led->ops->set == NULL) {
    return false;
  }

  // Cancel any existing blink on this LED before starting a new one.
  _hw_led_blink_cancel(led);

  // Reset the LED to a known-off state before starting the new blink.
  if (!led->ops->set(led, index, false)) {
    return false;
  }

  sys_timer_t *timer = sys_timer_init(period_ms, _hw_led_blink_timer_cb, led);
  if (timer == NULL) {
    return false;
  }

  _HW_LED_LOCK();
  led->blink.index = index;
  led->blink.desired = false;
  led->blink.flip_pending = false;
  led->blink.repeating = repeating;
  led->blink.timer = timer;
  _HW_LED_UNLOCK();

  // Increment the active-blink count before starting the timer, so the fast
  // path in _hw_led_poll() knows there's an active blink.
  sys_atomic_inc(&_hw_led_active_blinks);

  // Schedule the timer to start the blink.
  if (!sys_timer_start(timer)) {
    _HW_LED_LOCK();
    led->blink.timer = NULL;
    _HW_LED_UNLOCK();
    sys_atomic_dec(&_hw_led_active_blinks);
    sys_timer_deinit(timer);
    return false;
  }

  return true;
}
