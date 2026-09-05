#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <stddef.h>
#include <string.h>

// Same pico-only critical section as hid.c's device pool - see the note
// there.
#ifdef SYSTEM_NAME_PICO
#include "../sys/pico/sync.h"
#define _HID_LOCK() _sys_sync_pool_lock()
#define _HID_UNLOCK() _sys_sync_pool_unlock()
#else
#define _HID_LOCK()
#define _HID_UNLOCK()
#endif

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static bool _hid_timer_deinit(hid_device_t *device, void *userdata);
static void _hid_timer_callback(sys_timer_t *timer);

static const hid_device_callbacks_t _hid_timer_callbacks = {
    .deinit = _hid_timer_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

static bool _hid_timer_deinit(hid_device_t *device, void *userdata) {
  (void)userdata; // the caller's own data now - see hid_device_userdata()

  // For a one-shot timer, _hid_timer_callback() below has already
  // deinited the sys_timer_t and cleared device->context by the time this
  // runs (deferred via timer_remove_after_event) - the memcpy below reads
  // back NULL and there's nothing left to do.
  sys_timer_t *timer;
  memcpy(&timer, device->context, sizeof(timer));
  if (timer != NULL) {
    sys_timer_deinit(timer);
  }
  return true;
}

// A sys_timer_t callback only gets its own handle, so the owning device
// has to be found by scanning for it (keyed by the handle stored in
// device->context - see hid_register_timer()). May run in interrupt
// context on Pico (see sys/timer.h's platform note).
//
// The scan and the timer_repeating/userdata reads used to be separate
// _HID_LOCK() acquisitions; snapshotting them inside the same locked scan
// closes the same class of gap fixed in gpio.c's callback - see its
// comment.
static void _hid_timer_callback(sys_timer_t *timer) {
  hid_t *instance = _hid_singleton();
  if (instance == NULL) {
    return;
  }

  hid_device_t *device = NULL;
  bool repeating = false;
  void *userdata = NULL;

  _HID_LOCK();
  for (size_t i = 0; i < HID_DEVICE_CAPACITY; i++) {
    hid_device_t *candidate = &instance->devices[i];
    sys_timer_t *candidate_timer;
    memcpy(&candidate_timer, candidate->context, sizeof(candidate_timer));
    if (candidate->type == hid_type_timer && candidate_timer == timer) {
      device = candidate;
      repeating = candidate->timer_repeating;
      userdata = candidate->userdata;
      break;
    }
  }
  _HID_UNLOCK();

  if (device == NULL) {
    return;
  }

  if (!repeating) {
    // sys_timer_deinit() from inside its own callback is the documented
    // way to implement one-shot behavior. The hid_device_t pool slot
    // itself can't be released yet though - event->device (once the
    // retain/push below succeeds) must stay valid until the consumer
    // frees that event, so that's deferred to hid_event_free() via
    // timer_remove_after_event.
    //
    // This flag must be set *before* attempting the retain/push below,
    // not after: a failed push frees the event immediately (inside
    // _hid_event_push()), and hid_event_free() only runs its
    // deferred-deregister check if the flag is already true by then -
    // setting it afterward would silently leak this device slot forever
    // on a push failure.
    sys_timer_deinit(timer);
    _HID_LOCK();
    memset(device->context, 0, sizeof(timer));
    device->timer_remove_after_event = true;
    _HID_UNLOCK();
  }

  hid_event_t *event =
      _hid_event_retain(device->instance, hid_event_type_timer);
  if (event != NULL) {
    event->device = device;
    event->data.timer.userdata = userdata;
    _hid_event_push(event);
  } else if (!repeating) {
    // No event slot was available to ever carry the deferred deregister
    // above through to hid_event_free() - deregister directly instead, or
    // this already-expired one-shot device would sit in the pool forever
    // with nothing left to release it.
    hid_deregister(device->instance, device);
  }
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register_timer(hid_t *instance, uint32_t id,
                                 uint32_t interval_ms, bool repeating,
                                 void *userdata) {
  if (interval_ms == 0) {
    return NULL;
  }

  // No userdata passed to sys_timer_init() - hid_timer_t.userdata is
  // filled from device->userdata directly at fire time instead (see
  // _hid_timer_callback() above), now that device->userdata is always the
  // caller's own data rather than an indirection through the timer.
  sys_timer_t *timer = sys_timer_init(interval_ms, _hid_timer_callback, NULL);
  if (timer == NULL) {
    return NULL;
  }

  hid_device_t *device = hid_register(instance, "timer", id, hid_type_timer,
                                      hid_class_unknown, 0, userdata,
                                      _hid_timer_callbacks);
  if (device == NULL) {
    sys_timer_deinit(timer);
    return NULL;
  }

  _HID_LOCK();
  memcpy(device->context, &timer, sizeof(timer));
  device->timer_repeating = repeating;
  device->timer_remove_after_event = false;
  _HID_UNLOCK();

  // Started only now that context/timer_repeating/timer_remove_after_event
  // are all set - starting any earlier (e.g. from a hid_register() .init
  // callback, as this used to) risks the timer firing - and
  // _hid_timer_callback() reading timer_repeating - before those fields
  // are written, which for a very short interval_ms is a real, not just
  // theoretical, race: it would read the zero-initialized default (false)
  // and immediately deinit a timer meant to repeat.
  if (!sys_timer_start(timer)) {
    hid_deregister(instance, device);
    return NULL;
  }

  return device;
}
