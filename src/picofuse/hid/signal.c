#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// sys_env_signalhandler() is itself a single process-wide slot - only one
// hid_register_signal() registration can ever be meaningfully active, so
// this is a plain static rather than something tracked per-device.
static hid_device_t *_hid_signal_device = NULL;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static bool _hid_signal_deinit(hid_device_t *device, void *userdata);

static const hid_device_callbacks_t _hid_signal_callbacks = {
    .deinit = _hid_signal_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// Registered process-wide via sys_env_signalhandler() - see its own doc:
// may run in interrupt context on some platforms, so this stays minimal.
static void _hid_signal_callback(sys_env_signal_t signal) {
  if (_hid_signal_device != NULL && signal != sys_env_signal_none) {
    hid_event_queue_signal(_hid_signal_device, signal);
  }
}

static bool _hid_signal_deinit(hid_device_t *device, void *userdata) {
  (void)device;
  (void)userdata;
  _hid_signal_device = NULL;
  return sys_env_signalhandler(sys_env_signal_none, NULL);
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register_signal(hid_t *instance, void *userdata) {
  // Only one signal-observer registration at a time, and nothing to
  // register on a platform with no signal support at all (e.g. Pico,
  // where sys_env_signalhandler() is a stub that always returns false).
  if (_hid_signal_device != NULL) {
    return NULL;
  }
  if (!sys_env_signalhandler(sys_env_signal_none, _hid_signal_callback)) {
    return NULL;
  }

  hid_device_t *device = hid_register(instance, "signal", 0, hid_type_signal,
                                      hid_class_unknown, 0, userdata,
                                      _hid_signal_callbacks);
  if (device == NULL) {
    sys_env_signalhandler(sys_env_signal_none, NULL);
    return NULL;
  }

  _hid_signal_device = device;
  return device;
}
