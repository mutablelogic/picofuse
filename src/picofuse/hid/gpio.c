#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/hw.h>
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
// GLOBALS

static const char *_hid_gpio_name = "gpio-input";

// Tracks how many gpio-backed HID devices are currently registered, so the
// single process-wide hw_gpio_set_callback() hook is installed on the
// first one and removed after the last - rather than hid.c itself needing
// to know anything gpio-specific (there's no per-instance gpio lifecycle
// to hook into, unlike hid_register_signal()'s single static device).
static sys_atomic_t _hid_gpio_device_count = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static bool _hid_gpio_device_deinit(hid_device_t *device, void *userdata);

static const hid_device_callbacks_t _hid_gpio_device_callbacks = {
    .deinit = _hid_gpio_device_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// The single, global GPIO edge callback shared by every gpio-backed HID
// device - see _hid_gpio_device_count above for why there's only one. May
// run in interrupt context (see hw_gpio_set_callback()'s own doc).
//
// The lookup scan and the keycode/invert reads used to be two separate
// _HID_LOCK() acquisitions (find the device, then read its fields
// afterward, unlocked). That left a window where a concurrent
// _hid_register_gpio() finishing registration on this exact (bank, pin) -
// or a concurrent hid_deregister() tearing it down - could be observed
// mid-update. Snapshotting both fields inside the same locked scan means
// this either sees the device fully registered or not found at all, never
// partially filled.
static void _hid_gpio_callback(uint8_t bank, uint8_t pin, hw_gpio_event_t event,
                               void *userdata) {
  (void)userdata;

  hid_t *instance = _hid_singleton();
  if (instance == NULL) {
    return;
  }

  uint32_t id = ((uint32_t)bank << 16) | (uint32_t)pin;
  hid_device_t *device = NULL;
  bool invert = false;
  uint16_t keycode = 0;

  _HID_LOCK();
  for (size_t i = 0; i < HID_DEVICE_CAPACITY; i++) {
    hid_device_t *candidate = &instance->devices[i];
    if (candidate->type == hid_type_gpio && candidate->id == id) {
      device = candidate;
      invert = candidate->gpio_invert;
      keycode = candidate->keycode;
      break;
    }
  }
  _HID_UNLOCK();

  if (device == NULL) {
    return;
  }

  hid_state_t rising_state = invert ? hid_state_off : hid_state_on;
  hid_state_t falling_state = invert ? hid_state_on : hid_state_off;

  if ((event & hw_gpio_rising) != 0) {
    hid_event_queue_keycode(device, rising_state, keycode);
  }
  if ((event & hw_gpio_falling) != 0) {
    hid_event_queue_keycode(device, falling_state, keycode);
  }
}

static bool _hid_gpio_device_deinit(hid_device_t *device, void *userdata) {
  (void)userdata; // the caller's own data now - see hid_device_userdata()

  hw_gpio_t *gpio;
  memcpy(&gpio, device->context, sizeof(gpio));
  hw_gpio_deinit(gpio);

  if (sys_atomic_dec(&_hid_gpio_device_count) == 0) {
    hw_gpio_set_callback(NULL, NULL);
  }
  return true;
}

// Wrap an already-initialized gpio handle as a HID device. Shared tail for
// _hid_register_gpio_mode() below (which creates the handle itself from
// bank/pin/mode) and userbutton.c's hid_register_user_button() (which gets
// an already-initialized handle from hw_gpio_init_userbutton()).
hid_device_t *_hid_register_gpio(hid_t *instance, hw_gpio_t *gpio,
                                 uint16_t keycode, bool invert,
                                 void *userdata) {
  uint32_t id =
      ((uint32_t)hw_gpio_bank(gpio) << 16) | (uint32_t)hw_gpio_pin(gpio);
  hid_device_t *device = hid_register(instance, _hid_gpio_name, id,
                                      hid_type_gpio, hid_class_unknown, 0,
                                      userdata, _hid_gpio_device_callbacks);
  if (device == NULL) {
    hw_gpio_deinit(gpio);
    return NULL;
  }

  // Locked to match _hid_gpio_callback()'s own locked read of keycode/
  // gpio_invert - see its comment for why. context isn't read by that
  // callback, but is set here under the same lock for consistency (and
  // because hid_device_handle() reads it without one of its own - see its
  // doc on why that's fine for a write that happens exactly once, before
  // the device is deregistered).
  _HID_LOCK();
  device->keycode = keycode;
  device->gpio_invert = invert;
  memcpy(device->context, &gpio, sizeof(gpio));
  _HID_UNLOCK();

  if (sys_atomic_inc(&_hid_gpio_device_count) == 1) {
    hw_gpio_set_callback(_hid_gpio_callback, NULL);
  }

  return device;
}

// Shared by hid_register_gpio_input()/_pullup()/_pulldown() below.
static hid_device_t *_hid_register_gpio_mode(hid_t *instance, uint8_t bank,
                                             uint8_t pin, uint16_t keycode,
                                             hw_gpio_mode_t mode, bool invert,
                                             void *userdata) {
  hw_gpio_t *gpio = hw_gpio_init(bank, pin, mode);
  if (gpio == NULL) {
    sys_debugf("hid", "gpio register failed: bank=%u pin=%u mode=%u",
               (unsigned)bank, (unsigned)pin, (unsigned)mode);
    return NULL;
  }
  return _hid_register_gpio(instance, gpio, keycode, invert, userdata);
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register_gpio_input(hid_t *instance, uint8_t bank,
                                      uint8_t pin, uint16_t keycode,
                                      void *userdata) {
  return _hid_register_gpio_mode(instance, bank, pin, keycode, hw_gpio_input,
                                 false, userdata);
}

hid_device_t *hid_register_gpio_pullup(hid_t *instance, uint8_t bank,
                                       uint8_t pin, uint16_t keycode,
                                       void *userdata) {
  return _hid_register_gpio_mode(instance, bank, pin, keycode, hw_gpio_pullup,
                                 false, userdata);
}

hid_device_t *hid_register_gpio_pulldown(hid_t *instance, uint8_t bank,
                                         uint8_t pin, uint16_t keycode,
                                         void *userdata) {
  return _hid_register_gpio_mode(instance, bank, pin, keycode,
                                 hw_gpio_pulldown, false, userdata);
}
