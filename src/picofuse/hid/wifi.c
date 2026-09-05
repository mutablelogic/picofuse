#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <stddef.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// Forwarded from whatever called hw_wifi_set_callback() on our behalf in
// hid_register_wifi() below - userdata is the hid_device_t* we passed
// there, not the caller's own userdata (there is none: HID only observes).
static void _hid_wifi_callback(hw_wifi_t *wifi, hw_wifi_event_t event,
                               const hw_wifi_network_t *network,
                               void *userdata) {
  (void)wifi;
  hid_device_t *device = (hid_device_t *)userdata;
  hid_event_queue_wifi(device, event, network);
}

static bool _hid_wifi_device_deinit(hid_device_t *device, void *userdata) {
  (void)userdata; // the caller's own data now - see hid_device_userdata()

  hw_wifi_t *wifi;
  memcpy(&wifi, device->context, sizeof(wifi));

  // HID doesn't own this handle - only detach, never hw_wifi_deinit() it.
  // See hid_register_wifi()'s own doc.
  hw_wifi_set_callback(wifi, NULL, NULL);
  return true;
}

static const hid_device_callbacks_t _hid_wifi_callbacks = {
    .deinit = _hid_wifi_device_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register_wifi(hid_t *instance, hw_wifi_t *wifi,
                                void *userdata) {
  if (wifi == NULL) {
    return NULL;
  }

  hid_device_t *device = hid_register(instance, "wifi", 0, hid_type_wifi,
                                      hid_class_unknown, 0, userdata,
                                      _hid_wifi_callbacks);
  if (device == NULL) {
    return NULL;
  }

  memcpy(device->context, &wifi, sizeof(wifi));

  // Replaces any callback already attached to wifi - see this function's
  // own doc on why that's the caller's problem to avoid, not something
  // this can detect (hw_wifi_t has no "current callback" getter).
  hw_wifi_set_callback(wifi, _hid_wifi_callback, device);
  return device;
}
