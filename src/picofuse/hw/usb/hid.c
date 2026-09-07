#include <picofuse/hid.h>
#include <picofuse/hw.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS
//
// hw_usb_t is a process-wide singleton (see hw_usb_init()'s own doc) - so
// only one registration of it can ever be active at a time, matching
// net_ntp_register_hid()'s own reasoning for why this needs a plain static
// variable rather than hid_device_t's own private context storage: this
// file lives outside picofuse/hid and only has the public picofuse/hid.h
// API to work with, where the one pointer-sized slot a caller can attach
// to a device (userdata) is reserved for whatever hw_usb_register_hid()'s
// own caller wants to retrieve later via hid_device_userdata() - not for
// this file's own bookkeeping.
static hw_usb_t *_hw_usb_hid_usb = NULL;

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// Forwarded from whatever called hw_usb_set_callback() on our behalf in
// hw_usb_register_hid() below - userdata is the hid_device_t* we passed
// there, not the caller's own userdata (there is none: HID only observes).
static void _hw_usb_hid_callback(hw_usb_t *usb, hw_usb_event_t event,
                                 const hw_usb_device_t *device,
                                 void *userdata) {
  (void)usb;
  hid_device_t *hid_device = (hid_device_t *)userdata;
  hid_event_queue_usb(hid_device, event, device);
}

static bool _hw_usb_hid_deinit(hid_device_t *device, void *userdata) {
  (void)device;
  (void)userdata; // the caller's own data now - see hid_device_userdata()

  // HID doesn't own this handle - only detach, never hw_usb_deinit() it.
  // See hw_usb_register_hid()'s own doc.
  if (_hw_usb_hid_usb != NULL) {
    hw_usb_set_callback(_hw_usb_hid_usb, NULL, NULL);
  }
  _hw_usb_hid_usb = NULL;
  return true;
}

static const hid_device_callbacks_t _hw_usb_hid_callbacks = {
    .deinit = _hw_usb_hid_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hw_usb_register_hid(hid_t *instance, hw_usb_t *usb) {
  if (instance == NULL || usb == NULL || _hw_usb_hid_usb != NULL) {
    return NULL;
  }

  hid_device_t *device = hid_register(instance, "usb", 0, hid_type_usb,
                                      hid_class_unknown, 0, NULL,
                                      _hw_usb_hid_callbacks);
  if (device == NULL) {
    return NULL;
  }

  _hw_usb_hid_usb = usb;

  // Replaces any callback already attached to usb - see this function's
  // own doc on why that's the caller's problem to avoid, not something
  // this can detect (hw_usb_t has no "current callback" getter).
  hw_usb_set_callback(usb, _hw_usb_hid_callback, device);
  return device;
}
