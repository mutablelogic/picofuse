#pragma once
#include "led.h"
#include <picofuse/sys.h>

#ifdef SYSTEM_NAME_PICO
#include "../../sys/pico/sync.h"
// A critical_section_t (see sys/pico/sync.c), not a mutex - genuinely
// safe to take from an interrupt context, unlike most locking in this
// codebase. blink.c's own timer callback (a real hardware alarm IRQ on
// Pico - see sys_timer_t's own platform note) depends on that: it's the
// only thing here allowed to touch hw_blink_state_t from an ISR.
#define _HW_LED_LOCK() _sys_sync_pool_lock()
#define _HW_LED_UNLOCK() _sys_sync_pool_unlock()
#else
#include <pthread.h>
// sys_timer callbacks run on a genuine background thread on both host
// platforms - Linux's SIGEV_THREAD (see sys/linux/timer.c) and Darwin's
// libdispatch worker queue (see sys/darwin/timer.c) - unlike Pico's IRQ,
// where a critical section is the only thing that can touch this state
// concurrently. A no-op lock here would leave blink.c's timer callback
// racing hw_led_set()/hw_led_clear()/_hw_led_poll() for real, each
// potentially running on a different application thread. Plain static
// storage, initialized at compile time, mirrors the same per-timer lock
// sys/linux/timer.c and sys/darwin/timer.c already use for their own pool
// state. Defined once in blink.c; every translation unit that includes
// this header (led.c too) links against that same instance.
extern pthread_mutex_t _hw_led_lock;
#define _HW_LED_LOCK() pthread_mutex_lock(&_hw_led_lock)
#define _HW_LED_UNLOCK() pthread_mutex_unlock(&_hw_led_lock)
#endif

///////////////////////////////////////////////////////////////////////////////
// SHARED INTERNAL STATE
//
// led.c/blink.c are the core LED module, not a backend - see led.h's own
// doc on why a backend never sees this layout, only the opaque `context`
// scratch buffer. This header is how the two share the real hw_led_t
// definition across translation units, the same way net/pico/private.h
// does for socket.c/listener.c.

/**
 * @brief hw_led_blink()'s own state - one active blink per hw_led_t
 * handle, a real hardware limitation for NeoPixel (see hw_led_blink()'s
 * own doc), so this is a plain embedded member, not a pool of its own.
 *
 * A backend's own ops->set() can block (the Wi-Fi LED type's, for one,
 * takes a lock the CYW43 driver doesn't guarantee is safe to acquire from
 * an interrupt context), so blink.c's timer callback never calls it
 * directly - it just records what ought to happen (@ref desired, @ref
 * flip_pending) under @ref _HW_LED_LOCK, and _hw_led_poll() (run from
 * ordinary hw_poll() context, never from the timer ISR) is what actually
 * applies it. Every field here is read or written from that ISR at some
 * point, so touch all of them only under @ref _HW_LED_LOCK /
 * @ref _HW_LED_UNLOCK.
 */
typedef struct {
  sys_timer_t *timer; // NULL when no blink is active on this handle
  uint8_t index;      // which LED index the active blink applies to
  bool desired;        // on/off state _hw_led_poll() should apply next
  bool flip_pending;   // set by the timer callback, cleared once
                       // _hw_led_poll() has applied `desired`
  bool repeating;      // keep flipping until canceled, vs. stop after the
                       // first flip - see hw_led_blink()'s own doc
  pix_color_t on_color; // captured once, at hw_led_blink() call time, via
                        // ops->get_color and forced to full alpha - see
                        // hw_led_blink()'s own doc. Only meaningful when
                        // ops->set_color is non-NULL (NeoPixel); every
                        // other backend's ops->set(..., true) is already
                        // unconditionally full-on, so _hw_led_poll() never
                        // even reads this for them.
} hw_blink_state_t;

/**
 * @brief LED handle (shared across all backends).
 *
 * Deliberately private to led.c/blink.c - a backend never sees this
 * layout, only the `ops` it handed to `_hw_led_alloc()` and the embedded
 * `context` scratch buffer it gets back via `_hw_led_context()`.
 */
struct hw_led_t {
  const hw_led_ops_t *ops;
  hw_blink_state_t blink;
  _Alignas(max_align_t) uint8_t context[HW_LED_CONTEXT_SIZE];
};

static inline bool _hw_led_valid(const hw_led_t *led) {
  return led != NULL && led->ops != NULL;
}

// Defined in led.c - blink.c's own _hw_led_poll() iterates every slot
// (active or not; _hw_led_valid() screens out unclaimed ones) looking for
// a pending flip to apply.
extern hw_led_t _hw_led_pool[HW_LED_POOL_CAPACITY];

// Defined in blink.c - a count, not a bool: with HW_LED_POOL_CAPACITY
// handles each able to run their own independent blink, a plain "is
// *any* blink active" flag would go wrong the moment two are active at
// once and one of them gets canceled. _hw_led_poll()'s own fast path
// skips its pool scan entirely while this is 0 - every path that starts
// or stops a blink (hw_led_blink() itself, _hw_led_blink_cancel() below,
// and the timer callback's own self-stop for a non-repeating blink) is
// responsible for keeping it in sync. sys_atomic_t's inc/dec are safe
// from the timer's own ISR context on Pico, same as everything else
// shared with it here.
extern sys_atomic_t _hw_led_active_blinks;

/** @brief Stop and release any active blink timer on @p led, a no-op if
 * none is active - shared by hw_led_set()/hw_led_clear()/hw_led_deinit()
 * (each of which cancels a blink per hw_led_blink()'s own doc) and
 * blink.c itself.
 *
 * Captures/clears the timer pointer under the lock, then calls
 * sys_timer_deinit() outside it - never the other way around.
 * sys_timer_deinit() blocks until any in-progress callback finishes, and
 * that callback needs @ref _HW_LED_LOCK for itself; holding the lock
 * across the call would deadlock the two against each other.
 *
 * Also clears any pending-but-unapplied flip, both up front and again
 * after sys_timer_deinit() returns. Without the first clear, a one-shot
 * blink's final flip (queued by the timer callback, which already cleared
 * @ref hw_blink_state_t::timer itself before this ever runs - see
 * blink.c's own comment on why) would otherwise survive a cancel here
 * untouched, and _hw_led_poll() would apply it - and decrement @ref
 * _hw_led_active_blinks for it - later, *after* the caller's own
 * hw_led_set()/hw_led_clear() has already applied its own explicit state,
 * silently overriding it and double-decrementing the count this function
 * already accounted for. Without the second clear, a repeating blink's
 * timer callback can still fire once more between the first unlock above
 * and the sys_timer_deinit() call below (deinit only guarantees no
 * callback is *still running* once it returns, not that none starts
 * before it's called), leaving the exact same stale flip behind. */
static inline void _hw_led_blink_cancel(hw_led_t *led) {
  _HW_LED_LOCK();
  sys_timer_t *timer = led->blink.timer;
  bool had_pending = led->blink.flip_pending;
  led->blink.timer = NULL;
  led->blink.flip_pending = false;
  _HW_LED_UNLOCK();

  if (timer != NULL) {
    sys_timer_deinit(timer);
    _HW_LED_LOCK();
    led->blink.flip_pending = false;
    _HW_LED_UNLOCK();
  }

  if (timer != NULL || had_pending) {
    sys_atomic_dec(&_hw_led_active_blinks);
  }
}
