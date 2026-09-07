#include "private.h"

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// See private.h's own doc on why this is a count, not a bool.
sys_atomic_t _hw_led_active_blinks;

#ifndef SYSTEM_NAME_PICO
// See private.h's own doc on why this needs to be a real lock on host
// platforms - sys_timer callbacks there run on a genuine background
// thread, unlike Pico's IRQ context.
pthread_mutex_t _hw_led_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Fires every period_ms for as long as a blink is active - on Pico, this
// is a real hardware alarm IRQ (see sys_timer_t's own platform note), so
// it must stay ISR-safe: no calling into a backend's own ops->set(),
// which for at least the Wi-Fi LED type takes a lock the CYW43 driver
// doesn't guarantee is safe to acquire from an interrupt context (real
// hardware testing hung solid the first time this called ops->set()
// directly from here). All this does is flip `desired` and mark
// `flip_pending` under the lock; _hw_led_poll() (run from ordinary
// hw_poll() context) is what actually calls ops->set().
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
      // One flip and done - hw_led_blink() can start a new one again
      // once this clears. The flip itself still lands via _hw_led_poll()
      // having set flip_pending regardless - and it's _hw_led_poll()
      // that later decrements _hw_led_active_blinks too (see its own
      // comment on why: not here).
      led->blink.timer = NULL;
      stop = true;
    }
  }
  _HW_LED_UNLOCK();

  if (stop) {
    // Safe to call from inside its own callback - see sys_timer_init()'s
    // own doc. Never called while holding _HW_LED_LOCK - see
    // _hw_led_blink_cancel()'s own doc on why that would deadlock.
    sys_timer_deinit(timer);
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE (module)

// Called from hw_poll() - see hw/*/init.c. Applies whatever
// _hw_led_blink_timer_cb() above has queued up, from an ordinary,
// non-interrupt context where a backend's own ops->set() is always safe
// to call.
void _hw_led_poll(void) {
  // Fast path: skip the pool scan entirely while nothing has an active
  // blink. sys_atomic_get() is a single cheap read, cheaper than even an
  // empty pass over HW_LED_POOL_CAPACITY handles under the lock, and
  // hw_poll() (so this) runs on every single runloop tick.
  if (sys_atomic_get(&_hw_led_active_blinks) == 0) {
    return;
  }

  for (size_t i = 0; i < HW_LED_POOL_CAPACITY; i++) {
    hw_led_t *led = &_hw_led_pool[i];

    _HW_LED_LOCK();
    bool pending = _hw_led_valid(led) && led->blink.flip_pending;
    bool desired = led->blink.desired;
    uint8_t index = led->blink.index;
    // The timer callback clears its own `timer` field before this flip
    // was ever applied, for a non-repeating blink's one and only flip
    // (see its own comment) - so pending && timer == NULL uniquely means
    // "this is that blink's last flip", never a repeating blink's
    // (which keeps `timer` set for as long as it keeps running).
    bool finished = pending && led->blink.timer == NULL;
    if (pending) {
      led->blink.flip_pending = false;
    }
    _HW_LED_UNLOCK();

    if (pending && led->ops->set != NULL) {
      led->ops->set(led, index, desired);
    }
    if (finished) {
      // Only now, after the flip it was waiting on has actually been
      // applied - decrementing this in the timer callback itself, before
      // _hw_led_poll() got a chance to run, would let the fast path above
      // skip this very flip on the next tick (count already back at 0)
      // and leave the LED never actually turned on.
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

  // Only one blink can ever be active per handle - see this function's
  // own doc on why. Cancel whatever's already running (a no-op if
  // nothing is) rather than rejecting the new request, so switching
  // which NeoPixel index is blinking is just another hw_led_blink()
  // call, not a cancel-then-retry dance.
  _hw_led_blink_cancel(led);

  // Always start from a known-off state - _hw_led_poll() does all the
  // flipping from here (via the timer callback above queuing it up), on
  // the first flip turning this on.
  led->blink.index = index;
  led->blink.desired = false;
  led->blink.flip_pending = false;
  led->blink.repeating = repeating;
  // Backends can reject this - an out-of-range NeoPixel index, or a flush
  // that timed out (see led_neopixel.c's own _set()) - same as
  // hw_led_set() itself propagates. Don't start a timer that can never
  // successfully apply its own first flip.
  if (!led->ops->set(led, index, false)) {
    return false;
  }

  sys_timer_t *timer =
      sys_timer_init(period_ms, _hw_led_blink_timer_cb, led);
  if (timer == NULL) {
    return false;
  }
  if (!sys_timer_start(timer)) {
    sys_timer_deinit(timer);
    return false;
  }
  led->blink.timer = timer;
  sys_atomic_inc(&_hw_led_active_blinks);
  return true;
}
