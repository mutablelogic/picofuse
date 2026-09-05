#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pico is the only platform in this build where the device pool below can
// genuinely be raced from two physical cores at once - see the same
// pattern in hw/led/led.c and hw/deviceio/deviceio.c. Host platforms keep
// the plain unlocked scan.
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

// hid_t is a process-wide singleton - there is only ever one HID instance.
// A NULL queue marks it as not (yet) initialized, so there's no separate
// "active" flag to keep in sync.
static hid_t _hw_hid_instance;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Report whether the singleton is initialized.
static inline bool _hid_valid(const hid_t *instance) {
  return instance != NULL && instance == &_hw_hid_instance &&
         instance->queue != NULL;
}

// Check whether a device pointer belongs to the singleton's device pool.
static inline bool _hid_device_belongs(const hid_device_t *device) {
  ptrdiff_t slot;
  if (device == NULL) {
    return false;
  }
  slot = device - _hw_hid_instance.devices;
  return slot >= 0 && (size_t)slot < HID_DEVICE_CAPACITY;
}

// Give other hid/*.c translation units (timer.c, gpio.c, ...) access to the
// singleton, e.g. to scan instance->devices[] for a device that a raw
// backend handle (a sys_timer_t*, ...) belongs to - something the public
// API has no lookup for. Returns NULL when not (yet) initialized, same as
// _hid_valid() would report.
hid_t *_hid_singleton(void) {
  return _hw_hid_instance.queue != NULL ? &_hw_hid_instance : NULL;
}

// Retain a free device slot and fill in every field hid_register() takes,
// all under one lock acquisition - type is set last, since that's what
// marks a slot "in use" to any concurrent scanner (hid_poll(), the gpio/
// timer IRQ callbacks' own locked lookups, ...). Filling every field here
// rather than letting hid_register() write the rest afterward means a
// scanner can never observe a slot that's "in use" but only partially
// initialized (e.g. a device with .callbacks.read already set but
// .userdata still NULL).
static hid_device_t *_hid_device_retain(hid_t *instance, const char *name,
                                        uint32_t id, hid_type_t type,
                                        hid_class_t hid_class,
                                        uint32_t polling_interval_ms,
                                        void *userdata,
                                        hid_device_callbacks_t callbacks) {
  if (!_hid_valid(instance)) {
    return NULL;
  }

  _HID_LOCK();
  for (size_t i = 0; i < HID_DEVICE_CAPACITY; i++) {
    hid_device_t *device = &instance->devices[i];
    if (device->type == hid_type_none) {
      *device = (hid_device_t){0};
      device->instance = instance;
      device->name = name;
      device->id = id;
      device->hid_class = hid_class;
      device->polling_interval_ms = polling_interval_ms;
      device->userdata = userdata;
      device->callbacks = callbacks;
      device->type = type;
      _HID_UNLOCK();
      return device;
    }
  }
  _HID_UNLOCK();

  sys_debugf("hid", "device pool exhausted (capacity=%u)",
             (unsigned)HID_DEVICE_CAPACITY);
  return NULL;
}

// Release a previously retained device slot back to the pool.
static void _hid_device_release(hid_device_t *device) {
  if (!_hid_device_belongs(device)) {
    return;
  }
  _HID_LOCK();
  *device = (hid_device_t){0};
  _HID_UNLOCK();
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hid_t *hid_init(sys_event_queue_t *queue) {
  sys_debugf("hid", "hid_init: queue=%p", (void *)queue);
  if (queue == NULL) {
    return NULL;
  }
  if (_hw_hid_instance.queue != NULL) {
    sys_debugf("hid", "hid_init: already initialized, rejecting");
    return NULL;
  }

  _hw_hid_instance = (hid_t){0};
  _hw_hid_instance.queue = queue;
  return &_hw_hid_instance;
}

void hid_deinit(hid_t *instance) {
  sys_debugf("hid", "hid_deinit: instance=%p", (void *)instance);
  if (!_hid_valid(instance)) {
    return;
  }

  for (size_t i = 0; i < HID_DEVICE_CAPACITY; i++) {
    if (instance->devices[i].type != hid_type_none) {
      hid_deregister(instance, &instance->devices[i]);
    }
  }
  _hw_hid_instance = (hid_t){0};
}

bool hid_poll(hid_t *instance) {
  if (!_hid_valid(instance)) {
    return false;
  }

  bool processed = false;
  uint64_t poll_time_ms = sys_timestamp_ms();
  for (size_t i = 0; i < HID_DEVICE_CAPACITY; i++) {
    hid_device_t *device = &instance->devices[i];

    // Snapshot everything this iteration needs while locked, so a
    // concurrent hid_deregister() zeroing this exact slot mid-iteration
    // can't be observed as a torn read (e.g. .callbacks.read already
    // NULLed out from under a .userdata we already fetched, or vice
    // versa). The read callback itself still runs unlocked below - it may
    // block/take time, and backend callbacks never run under this lock
    // (same convention as hid_register()'s .init and hid_deregister()'s
    // .deinit).
    _HID_LOCK();
    bool due = device->type != hid_type_none && device->callbacks.read != NULL &&
              (device->polling_interval_ms == 0 || device->last_event_ms == 0 ||
               (poll_time_ms - device->last_event_ms) >=
                   device->polling_interval_ms);
    bool (*read_fn)(hid_device_t *, void *) = NULL;
    void *userdata = NULL;
    if (due) {
      device->last_event_ms = poll_time_ms;
      read_fn = device->callbacks.read;
      userdata = device->userdata;
    }
    _HID_UNLOCK();

    if (read_fn != NULL && read_fn(device, userdata)) {
      processed = true;
    }
  }

  return processed;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register(hid_t *instance, const char *name, uint32_t id,
                           hid_type_t type, hid_class_t hid_class,
                           uint32_t polling_interval_ms, void *userdata,
                           hid_device_callbacks_t callbacks) {
  hid_device_t *device =
      _hid_device_retain(instance, name, id, type, hid_class,
                         polling_interval_ms, userdata, callbacks);
  if (device == NULL) {
    return NULL;
  }

  // Release the device again if backend init fails. Runs unlocked -
  // backend init may block/take time, so it never runs under the pool
  // lock (same convention as .deinit in hid_deregister()).
  if (device->callbacks.init != NULL &&
      !device->callbacks.init(device, device->userdata)) {
    _hid_device_release(device);
    return NULL;
  }

  sys_debugf("hid", "device registered: name=%s id=%08X type=%u", device->name,
             (unsigned)device->id, (unsigned)device->type);
  return device;
}

bool hid_deregister(hid_t *instance, hid_device_t *device) {
  if (!_hid_valid(instance) || !_hid_device_belongs(device) ||
      device->type == hid_type_none) {
    return false;
  }

  // Logged before running teardown callbacks, since a backend's .deinit may
  // free/null fields such as device->name.
  sys_debugf("hid", "device de-registered: name=%s id=%08X type=%u",
             device->name, (unsigned)device->id, (unsigned)device->type);

  if (device->callbacks.deinit != NULL) {
    device->callbacks.deinit(device, device->userdata);
  }

  _hid_device_release(device);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

hid_device_t *hid_device_next(hid_device_t *device) {
  size_t start = 0;
  if (device != NULL) {
    if (!_hid_device_belongs(device)) {
      return NULL;
    }
    start = (size_t)(device - _hw_hid_instance.devices) + 1;
  }

  for (size_t i = start; i < HID_DEVICE_CAPACITY; i++) {
    if (_hw_hid_instance.devices[i].type != hid_type_none) {
      return &_hw_hid_instance.devices[i];
    }
  }
  return NULL;
}

bool hid_device_info(const hid_device_t *device, const char **out_name,
                     uint32_t *out_id, hid_type_t *out_type,
                     hid_class_t *out_class) {
  if (out_name != NULL) {
    *out_name = NULL;
  }
  if (out_id != NULL) {
    *out_id = 0;
  }
  if (out_type != NULL) {
    *out_type = hid_type_none;
  }
  if (out_class != NULL) {
    *out_class = hid_class_unknown;
  }

  if (!_hid_device_belongs(device) || device->type == hid_type_none) {
    return false;
  }

  if (out_name != NULL) {
    *out_name = device->name;
  }
  if (out_id != NULL) {
    *out_id = device->id;
  }
  if (out_type != NULL) {
    *out_type = device->type;
  }
  if (out_class != NULL) {
    *out_class = device->hid_class;
  }
  return true;
}

void *hid_device_userdata(const hid_device_t *device) {
  if (!_hid_device_belongs(device) || device->type == hid_type_none) {
    return NULL;
  }
  return device->userdata;
}

void *hid_device_handle(const hid_device_t *device) {
  if (!_hid_device_belongs(device) || device->type == hid_type_none) {
    return NULL;
  }

  // By convention, a backend that owns one meaningful single handle
  // (hw_gpio_t*/hw_wifi_t*/sys_timer_t*/hw_adc_t*) stores it as the first
  // pointer-sized slot of device->context - true even for adc.c's larger
  // _hid_adc_ctx_t, whose first field is deliberately its hw_adc_t*.
  // Types with no such handle (hid_type_signal, a generic hid_register()
  // device) leave context zeroed, so this naturally returns NULL for them.
  void *handle;
  memcpy(&handle, device->context, sizeof(handle));
  return handle;
}
