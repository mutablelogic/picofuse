#include <picofuse/hid.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>

// Frequent enough to notice clock drift, infrequent enough not to hammer
// the server - see net_ntp_register_hid()'s own doc.
#ifndef NET_NTP_HID_DEFAULT_POLLING_INTERVAL_MS
#define NET_NTP_HID_DEFAULT_POLLING_INTERVAL_MS (60u * 60u * 1000u)
#endif

///////////////////////////////////////////////////////////////////////////////
// GLOBALS
//
// A singleton, matching net_ntp_t's own reasoning (only one NTP
// connection - and so only one registration of it - ever exists at a
// time). This can't live in hid_device_t's own per-device context
// storage the way hid/adc.c's or hid/wifi.c's backends do: those files
// are compiled as part of the hid module itself and can see
// hid_device_t's private layout (src/picofuse/hid/private.h); this file
// lives in net instead and only has the public picofuse/hid.h API to
// work with, where the one pointer-sized slot a caller can attach to a
// device (`userdata`, passed straight through to hid_register() below)
// is reserved for whatever net_ntp_register_hid()'s own caller wants to
// retrieve later via hid_device_userdata() - not for this file's own
// bookkeeping.
static net_ntp_t *_net_ntp_hid_ntp = NULL;
static int64_t _net_ntp_hid_last_seconds = 0;

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

static bool _net_ntp_hid_read(hid_device_t *device, void *userdata) {
  (void)userdata;

  sys_date_t date;
  if (!net_ntp_read(_net_ntp_hid_ntp, &date)) {
    return false;
  }
  if (date.seconds == _net_ntp_hid_last_seconds) {
    return false;
  }
  _net_ntp_hid_last_seconds = date.seconds;
  return hid_event_queue_time(device, &date);
}

static bool _net_ntp_hid_deinit(hid_device_t *device, void *userdata) {
  (void)device;
  (void)userdata;
  // Doesn't own ntp - net_ntp_deinit() is still the caller's own
  // responsibility, see net_ntp_register_hid()'s own doc.
  _net_ntp_hid_ntp = NULL;
  return true;
}

static const hid_device_callbacks_t _net_ntp_hid_callbacks = {
    .read = _net_ntp_hid_read,
    .deinit = _net_ntp_hid_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hid_device_t *net_ntp_register_hid(hid_t *instance, net_ntp_t *ntp,
                                   uint32_t polling_interval_ms,
                                   void *userdata) {
  if (ntp == NULL || _net_ntp_hid_ntp != NULL) {
    return NULL;
  }

  hid_device_t *device =
      hid_register(instance, "ntp", 0, hid_type_other, hid_class_sensor,
                  polling_interval_ms != 0
                      ? polling_interval_ms
                      : NET_NTP_HID_DEFAULT_POLLING_INTERVAL_MS,
                  userdata, _net_ntp_hid_callbacks);
  if (device == NULL) {
    return NULL;
  }

  _net_ntp_hid_ntp = ntp;
  _net_ntp_hid_last_seconds = 0;
  return device;
}
