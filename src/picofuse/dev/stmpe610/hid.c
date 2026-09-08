// Lives alongside stmpe610.c/stmpe610.h (declared in
// include/picofuse/dev/stmpe610.h, under its own "HID" section) and is
// compiled into picofuse-dev, same as stmpe610.c - see
// dev/ft6236/hid.c's own doc for the full reasoning (picofuse-hid must
// not know about specific device drivers, so picofuse-dev links
// picofuse-hid, not the reverse).
#include "../../hid/private.h"
#include <picofuse/dev/stmpe610.h>
#include <picofuse/hid.h>
#include <string.h>

// Purely an efficiency floor, not a correctness one - see
// dev_stmpe610_register_hid()'s own doc on why STMPE610's FIFO-backed
// reads don't have FT6236's torn-read hazard.
#define STMPE610_HID_MIN_POLLING_INTERVAL_MS 10u

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// Forwarded from dev_stmpe610_poll() via dev_stmpe610_set_callback() in
// dev_stmpe610_register_hid() below - userdata is the hid_device_t* we
// passed there, not the caller's own userdata (see hid_register_wifi()'s
// own doc on the same pattern). touch is already a hid_touch_t - nothing
// to translate, just forward its fields.
static void _dev_stmpe610_hid_touch(dev_stmpe610_t *stmpe610,
                                    const hid_touch_t *touch,
                                    void *userdata) {
  (void)stmpe610;
  hid_device_t *device = (hid_device_t *)userdata;
  hid_event_queue_touch(device, touch->state, touch->point, touch->slot,
                       touch->pressure);
}

static bool _dev_stmpe610_hid_read(hid_device_t *device, void *userdata) {
  (void)userdata;
  dev_stmpe610_t *stmpe610;
  memcpy(&stmpe610, device->context, sizeof(stmpe610));
  dev_stmpe610_poll(stmpe610);
  // Events (if any) were already pushed synchronously by
  // _dev_stmpe610_hid_touch() above - this return value only feeds
  // hid_poll()'s own aggregate "did anything run" result, not correctness.
  return true;
}

static bool _dev_stmpe610_hid_device_deinit(hid_device_t *device,
                                            void *userdata) {
  (void)userdata; // the caller's own data now - see hid_device_userdata()
  dev_stmpe610_t *stmpe610;
  memcpy(&stmpe610, device->context, sizeof(stmpe610));

  // HID doesn't own this handle - only detach, never dev_stmpe610_deinit()
  // it. See dev_stmpe610_register_hid()'s own doc.
  dev_stmpe610_set_callback(stmpe610, NULL, NULL);
  return true;
}

static const hid_device_callbacks_t _dev_stmpe610_hid_callbacks = {
    .read = _dev_stmpe610_hid_read,
    .deinit = _dev_stmpe610_hid_device_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *dev_stmpe610_register_hid(hid_t *instance,
                                        dev_stmpe610_t *stmpe610,
                                        uint32_t polling_interval_ms,
                                        void *userdata) {
  if (stmpe610 == NULL) {
    return NULL;
  }

  if (polling_interval_ms < STMPE610_HID_MIN_POLLING_INTERVAL_MS) {
    polling_interval_ms = STMPE610_HID_MIN_POLLING_INTERVAL_MS;
  }

  hid_device_t *device = hid_register(
      instance, "touch", 0, hid_type_other, hid_class_touchscreen,
      polling_interval_ms, userdata, _dev_stmpe610_hid_callbacks);
  if (device == NULL) {
    return NULL;
  }

  memcpy(device->context, &stmpe610, sizeof(stmpe610));

  // Replaces any callback already attached to stmpe610 - see this
  // function's own doc on why that's the caller's problem to avoid, not
  // something this can detect (dev_stmpe610_t has no "current callback"
  // getter).
  dev_stmpe610_set_callback(stmpe610, _dev_stmpe610_hid_touch, device);
  return device;
}
