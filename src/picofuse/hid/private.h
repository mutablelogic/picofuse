#pragma once
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Internal representation of a HID device. */
struct hid_device_t {
  // Device information
  hid_t *instance;
  const char *name;
  uint32_t id;
  hid_class_t hid_class;
  hid_device_callbacks_t callbacks;

  // Device state
  uint16_t keycode;
  hid_state_t state;
  hid_type_t type;

  // Polling/timer state
  uint32_t polling_interval_ms;
  uint64_t last_event_ms;
  bool timer_repeating;
  bool timer_remove_after_event;

  // GPIO state - see hid_register_gpio_input()/_pullup()/_pulldown() in
  // gpio.c. When true, a falling edge reports hid_state_on and a rising
  // edge hid_state_off (for a pull-up-wired, active-low input).
  bool gpio_invert;

  // The caller's own opaque pointer, from whichever hid_register*()
  // function's `userdata` parameter created this device - always this and
  // nothing else, returned verbatim by hid_device_userdata(). A backend's
  // own hardware/system handle (hw_gpio_t*, hw_wifi_t*, sys_timer_t*, ...)
  // never lives here - see context below.
  void *userdata;

  // Scratch space for a backend's own private per-device state - see
  // HID_DEVICE_CONTEXT_SIZE and hid_device_handle(). Unused by
  // hid_type_signal and generic hid_register() devices (nothing to store,
  // so this stays zeroed - hid_device_handle() reports NULL for them).
  // gpio.c/wifi.c/timer.c each store their single hw_gpio_t*/hw_wifi_t*/
  // sys_timer_t* handle as the first pointer-sized slot here; adc.c
  // overlays a larger struct (handles plus a last-seen reading for
  // change-detection) whose own first field is deliberately its
  // hw_adc_t*, for the same reason.
  _Alignas(max_align_t) uint8_t context[HID_DEVICE_CONTEXT_SIZE];
};

/** @brief Internal representation of a HID instance. */
struct hid_t {
  sys_event_queue_t *queue;
  hid_event_t events[HID_EVENT_CAPACITY];
  hid_device_t devices[HID_DEVICE_CAPACITY];
};

///////////////////////////////////////////////////////////////////////////////
// SHARED INTERNAL HELPERS
//
// hid.c/event.c/timer.c/... are all part of the same module and share
// these across translation units via this header, the same way
// hw/led/led.h shares _hw_led_alloc()/_hw_led_context() across
// led_gpio.c/led_pwm.c/etc.

// Defined in hid.c. Gives other hid/*.c files access to the singleton
// instance without exposing it as a global - e.g. to scan
// instance->devices[] for the device a raw backend handle belongs to.
// Returns NULL when not (yet) initialized.
hid_t *_hid_singleton(void);

// Defined in event.c. Retain a free event slot from instance->events[],
// pre-tagged with its payload type - the caller fills in the rest of the
// payload and pushes it with _hid_event_push().
hid_event_t *_hid_event_retain(hid_t *instance, hid_event_type_t type);

// Defined in event.c. Push a retained-and-filled event onto its owning
// instance's queue, releasing it back to the pool on failure (mirrors the
// push-or-free pattern every hid_event_queue_*() helper needs).
bool _hid_event_push(hid_event_t *event);

// Defined in gpio.c. Wraps an already-initialized gpio handle as a HID
// device; `invert` controls whether a rising or falling edge reports
// hid_state_on (see hid_device_t.gpio_invert). `userdata` is the caller's
// own opaque pointer (see hid_device_t.userdata) - gpio itself is stored
// in the new device's context instead (see hid_device_t.context). Used by
// hid_register_gpio_input()/_pullup()/_pulldown()'s own internal helper
// and by userbutton.c's hid_register_user_button(), which gets its handle
// from hw_gpio_init_userbutton() instead of calling hw_gpio_init() itself.
hid_device_t *_hid_register_gpio(hid_t *instance, hw_gpio_t *gpio,
                                 uint16_t keycode, bool invert,
                                 void *userdata);
