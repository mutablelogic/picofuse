#include "../../hid/private.h"
#include <picofuse/dev/ft6236.h>
#include <picofuse/hid.h>
#include <string.h>

// Real minimum, not an arbitrary default, and enforced unconditionally -
// even with an interrupt pin configured (see dev_ft6236_register_hid()'s
// own doc on why that still needs this). The FT6X36 datasheet's FEATURES
// claim "Report Rate: Up to 100Hz" (10ms), but its own Active Mode
// default is 60 frames/second (~16.7ms, section 2.3 "Operation Modes") -
// INT there is a per-frame "data ready" pulse, not a level held for the
// duration of a touch, so polling faster than the chip's own scan
// cadence risks reading its 15-byte register burst mid-update (observed
// on real hardware: a torn read misreported as a lift - state
// alternating on/off/repeat every poll, with the "off" always at a
// suspicious x=0/y=0). 20ms, comfortably above the 16.7ms Active Mode
// frame period, not the datasheet's own faster-sounding 10ms headline
// figure.
#define FT6236_HID_MIN_POLLING_INTERVAL_MS 20u

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// Forwarded from dev_ft6236_poll() via dev_ft6236_set_callback() in
// dev_ft6236_register_hid() below - userdata is the hid_device_t* we
// passed there, not the caller's own userdata (see hid_register_wifi()'s
// own doc on the same pattern). touch is already a hid_touch_t - nothing
// to translate, just forward its fields.
static void _dev_ft6236_hid_touch(dev_ft6236_t *ft6236,
                                  const hid_touch_t *touch, void *userdata) {
  (void)ft6236;
  hid_device_t *device = (hid_device_t *)userdata;
  hid_event_queue_touch(device, touch->state, touch->point, touch->slot,
                       touch->pressure);
}

// See dev_ft6236_register_hid()'s own doc on why this is a single
// fixed-interval poll regardless of whether an interrupt pin is
// configured - dev_ft6236_poll() itself already makes that distinction
// cheap, not this.
static bool _dev_ft6236_hid_read(hid_device_t *device, void *userdata) {
  (void)userdata;
  dev_ft6236_t *ft6236;
  memcpy(&ft6236, device->context, sizeof(ft6236));
  dev_ft6236_poll(ft6236);
  // Events (if any) were already pushed synchronously by
  // _dev_ft6236_hid_touch() above - this return value only feeds
  // hid_poll()'s own aggregate "did anything run" result, not correctness.
  return true;
}

static bool _dev_ft6236_hid_device_deinit(hid_device_t *device,
                                          void *userdata) {
  (void)userdata; // the caller's own data now - see hid_device_userdata()
  dev_ft6236_t *ft6236;
  memcpy(&ft6236, device->context, sizeof(ft6236));

  // HID doesn't own this handle - only detach, never dev_ft6236_deinit()
  // it. See dev_ft6236_register_hid()'s own doc.
  dev_ft6236_set_callback(ft6236, NULL, NULL);
  return true;
}

static const hid_device_callbacks_t _dev_ft6236_hid_callbacks = {
    .read = _dev_ft6236_hid_read,
    .deinit = _dev_ft6236_hid_device_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *dev_ft6236_register_hid(hid_t *instance, dev_ft6236_t *ft6236,
                                      uint32_t polling_interval_ms,
                                      void *userdata) {
  if (ft6236 == NULL) {
    return NULL;
  }

  // Enforced regardless of whether an interrupt pin is configured - see
  // FT6236_HID_MIN_POLLING_INTERVAL_MS's own doc on why an interrupt pin
  // doesn't make polling faster than this safe, even though it does make
  // each individual poll cheaper when nothing is pending.
  if (polling_interval_ms < FT6236_HID_MIN_POLLING_INTERVAL_MS) {
    polling_interval_ms = FT6236_HID_MIN_POLLING_INTERVAL_MS;
  }

  hid_device_t *device =
      hid_register(instance, "touch", 0, hid_type_other, hid_class_touchscreen,
                   polling_interval_ms, userdata, _dev_ft6236_hid_callbacks);
  if (device == NULL) {
    return NULL;
  }

  memcpy(device->context, &ft6236, sizeof(ft6236));

  // Replaces any callback already attached to ft6236
  dev_ft6236_set_callback(ft6236, _dev_ft6236_hid_touch, device);
  return device;
}
