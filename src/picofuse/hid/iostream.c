#include "private.h"
#include <picofuse/hid.h>
#include <stddef.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// Forwarded from whatever called sys_iostream_set_callback() on our
// behalf in hid_register_iostream() below - userdata is the hid_device_t*
// we passed there, not the caller's own userdata (there is none: HID only
// observes).
static void _hid_iostream_callback(sys_iostream_t *stream,
                                   sys_iostream_event_t events,
                                   void *userdata) {
  (void)stream;
  hid_device_t *device = (hid_device_t *)userdata;
  hid_event_queue_iostream(device, events);
}

static bool _hid_iostream_device_deinit(hid_device_t *device,
                                        void *userdata) {
  (void)userdata; // the caller's own data now - see hid_device_userdata()

  sys_iostream_t *stream;
  memcpy(&stream, device->context, sizeof(stream));

  // HID doesn't own this stream - only detach, never sys_iostream_close()
  // it. See hid_register_iostream()'s own doc.
  sys_iostream_set_callback(stream, NULL, NULL);
  return true;
}

static const hid_device_callbacks_t _hid_iostream_callbacks = {
    .deinit = _hid_iostream_device_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register_iostream(hid_t *instance, sys_iostream_t *stream,
                                    void *userdata) {
  if (stream == NULL) {
    return NULL;
  }

  hid_device_t *device =
      hid_register(instance, "iostream", 0, hid_type_iostream,
                  hid_class_unknown, 0, userdata, _hid_iostream_callbacks);
  if (device == NULL) {
    return NULL;
  }

  memcpy(device->context, &stream, sizeof(stream));

  // Replaces any callback already attached to stream - see this
  // function's own doc on why that's the caller's problem to avoid, not
  // something this can detect (sys_iostream_t has no "current callback"
  // getter).
  if (!sys_iostream_set_callback(stream, _hid_iostream_callback, device)) {
    hid_deregister(instance, device);
    return NULL;
  }

  return device;
}
