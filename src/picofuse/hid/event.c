#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <stddef.h>

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

// Lock keys (CapsLock/NumLock/ScrollLock) toggle their bit on each press
// rather than tracking held/released like other modifiers; see their
// handling in hid_event_queue_keycode(). Kept private rather than a public
// hid_state_t mask, since unlike e.g. hid_state_shift (left vs. right sides
// of the *same* modifier), the three lock keys are unrelated toggles, so
// "is any lock active" isn't a meaningful query to expose.
#define _HID_STATE_LOCK_MASK                                                  \
  (hid_state_caps_lock | hid_state_num_lock | hid_state_scroll_lock)

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Retain a free event slot from the pool, pre-tagged with its payload type
// so a claimed-but-not-yet-filled slot can never be mistaken for a free one.
hid_event_t *_hid_event_retain(hid_t *instance, hid_event_type_t type) {
  if (instance == NULL || type == hid_event_type_none) {
    return NULL;
  }

  _HID_LOCK();
  for (size_t i = 0; i < HID_EVENT_CAPACITY; i++) {
    hid_event_t *event = &instance->events[i];
    if (event->type == hid_event_type_none) {
      *event = (hid_event_t){0};
      event->type = type;
      _HID_UNLOCK();
      return event;
    }
  }
  _HID_UNLOCK();

  sys_debugf("hid", "event pool exhausted (capacity=%u)",
             (unsigned)HID_EVENT_CAPACITY);
  return NULL;
}

// Release a previously retained event slot back to the pool. Bounds-checks
// against the singleton directly rather than via event->device->instance -
// hid_event_free()'s timer auto-deregister case below clears the device
// slot (including its ->instance field) before this runs, so going through
// event->device would read a stale/zeroed pointer here instead.
static void _hid_event_release(hid_event_t *event) {
  hid_t *instance = _hid_singleton();
  if (instance == NULL || event == NULL) {
    return;
  }

  ptrdiff_t slot = event - instance->events;
  if (slot < 0 || (size_t)slot >= HID_EVENT_CAPACITY) {
    return;
  }

  _HID_LOCK();
  *event = (hid_event_t){0};
  _HID_UNLOCK();
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

// Push a retained-and-filled event onto its owning instance's queue,
// releasing it back to the pool if the push itself fails (e.g. queue full).
bool _hid_event_push(hid_event_t *event) {
  if (event == NULL || event->device == NULL || event->device->instance == NULL) {
    return false;
  }

  if (!sys_event_queue_try_push(event->device->instance->queue,
                                (sys_event_t)event)) {
    hid_event_free(event);
    return false;
  }
  return true;
}

bool hid_event_queue_keycode(hid_device_t *device, hid_state_t state,
                             uint16_t keycode) {
  if (device == NULL) {
    return false;
  }

  hid_event_t *event =
      _hid_event_retain(device->instance, hid_event_type_keycode);
  if (event == NULL) {
    return false;
  }

  // Modifier/lock keys fold into the device's persisted state rather than
  // just being reported per-event, so a later unrelated keycode still
  // reflects e.g. Shift being held. Lock keys are the exception: they
  // toggle once per press rather than tracking held/released.
  hid_state_t translated_state = hid_keycode_to_state(keycode);
  hid_state_t next_state = device->state;

  if ((state & hid_state_on) != 0) {
    if ((translated_state & _HID_STATE_LOCK_MASK) != 0) {
      next_state ^= translated_state;
    } else if (translated_state != hid_state_none) {
      next_state |= translated_state;
    }
    next_state |= hid_state_on;
    next_state &= ~(hid_state_t)hid_state_off;
  }

  if ((state & hid_state_off) != 0) {
    // Lock keys are left untouched on release - the toggle already
    // happened on press, and clearing here would immediately undo it.
    if (translated_state != hid_state_none &&
        (translated_state & _HID_STATE_LOCK_MASK) == 0) {
      next_state &= ~translated_state;
    }
    next_state |= hid_state_off;
    next_state &= ~(hid_state_t)hid_state_on;
  }

  device->state = next_state;
  device->keycode = keycode;

  // Transient one-shot annotations (auto-repeat, click counts) describe
  // this event only, so they're reported here without being folded into
  // device->state, where they'd otherwise incorrectly linger and show up
  // on unrelated later events.
  hid_state_t transient_state =
      state & (hid_state_repeat | hid_state_click | hid_state_double_click |
               hid_state_triple_click | hid_state_long_click);

  event->device = device;
  event->data.keycode.state = next_state | transient_state;
  event->data.keycode.keycode = keycode;
  return _hid_event_push(event);
}

bool hid_event_queue_metric_float(hid_device_t *device, const char *name,
                                  const char *unit, float value) {
  if (device == NULL || name == NULL || unit == NULL) {
    return false;
  }

  hid_event_t *event = _hid_event_retain(device->instance, hid_event_type_metric);
  if (event == NULL) {
    return false;
  }

  event->device = device;
  event->data.metric.name = name;
  event->data.metric.unit = unit;
  event->data.metric.value = value;
  return _hid_event_push(event);
}

bool hid_event_queue_signal(hid_device_t *device, sys_env_signal_t signal) {
  if (device == NULL || signal == sys_env_signal_none) {
    return false;
  }

  hid_event_t *event = _hid_event_retain(device->instance, hid_event_type_signal);
  if (event == NULL) {
    return false;
  }

  event->device = device;
  event->data.signal.signal = signal;
  return _hid_event_push(event);
}

bool hid_event_queue_wifi(hid_device_t *device, hw_wifi_event_t event_type,
                         const hw_wifi_network_t *network) {
  if (device == NULL) {
    return false;
  }

  hid_event_t *event = _hid_event_retain(device->instance, hid_event_type_wifi);
  if (event == NULL) {
    return false;
  }

  event->device = device;
  event->data.wifi.event = event_type;
  event->data.wifi.network = network;
  return _hid_event_push(event);
}

bool hid_event_queue_iostream(hid_device_t *device,
                              sys_iostream_event_t events) {
  if (device == NULL || events == sys_iostream_event_none) {
    return false;
  }

  hid_event_t *event =
      _hid_event_retain(device->instance, hid_event_type_iostream);
  if (event == NULL) {
    return false;
  }

  event->device = device;
  event->data.iostream.events = events;
  return _hid_event_push(event);
}

void hid_event_free(hid_event_t *event) {
  // A one-shot timer's sys_timer_t is already gone by the time its event
  // reaches here (see timer.c's _hid_timer_callback) - this is what
  // finally releases the hid_device_t pool slot back, deferred until the
  // consumer is done with the event (event->device must stay valid until
  // then).
  if (event != NULL && event->type == hid_event_type_timer &&
      event->device != NULL && event->device->timer_remove_after_event) {
    hid_t *instance = event->device->instance;
    hid_device_t *device = event->device;
    device->timer_remove_after_event = false;
    if (instance != NULL) {
      hid_deregister(instance, device);
    }
  }

  _hid_event_release(event);
}
