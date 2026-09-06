#include <picofuse/hid.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>

// Steady-state cadence once a sync has actually succeeded at least once -
// frequent enough to notice clock drift, infrequent enough not to hammer
// the server. See net_ntp_register_hid()'s own doc.
#ifndef NET_NTP_HID_DEFAULT_POLLING_INTERVAL_MS
#define NET_NTP_HID_DEFAULT_POLLING_INTERVAL_MS (60u * 60u * 1000u)
#endif

// How soon to retry after a *failed* read (no reply, no route yet, ...) -
// deliberately much shorter than the steady-state interval above. See
// _net_ntp_hid_read()'s own doc on why this can't just be
// polling_interval_ms itself.
#ifndef NET_NTP_HID_RETRY_INTERVAL_MS
#define NET_NTP_HID_RETRY_INTERVAL_MS (10u * 1000u)
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
static uint32_t _net_ntp_hid_interval_ms = 0;
static uint64_t _net_ntp_hid_next_attempt_ms = 0;

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

// hid_register() below is always given polling_interval_ms=0 ("evaluate
// on every hid_poll() call" - see hid_register()'s own doc), regardless
// of what net_ntp_register_hid()'s own caller asked for - the real
// cadence is self-throttled here via _net_ntp_hid_next_attempt_ms
// instead. This matters because hid_poll()'s own interval bookkeeping
// (last_event_ms) advances the moment a read is attempted, whether or
// not it actually succeeds: with a single, static polling_interval_ms, a
// a read that fails on its very first attempt (no network route yet, right
// after Wi-Fi joins, say) would otherwise be locked out for a full
// interval - up to an hour by default - before trying again, rather than
// retrying soon after. Splitting the two lets a failed attempt retry
// quickly (NET_NTP_HID_RETRY_INTERVAL_MS) while a succeeding one settles
// into the slower, caller-requested steady-state cadence.
static bool _net_ntp_hid_read(hid_device_t *device, void *userdata) {
  (void)userdata;

  uint64_t now = sys_timestamp_ms();
  if (now < _net_ntp_hid_next_attempt_ms) {
    return false;
  }

  sys_date_t date;
  if (!net_ntp_read(_net_ntp_hid_ntp, &date)) {
    _net_ntp_hid_next_attempt_ms = now + NET_NTP_HID_RETRY_INTERVAL_MS;
    sys_debugf("net", "ntp hid: read failed, retrying in %ums",
              (unsigned)NET_NTP_HID_RETRY_INTERVAL_MS);
    return false;
  }
  _net_ntp_hid_next_attempt_ms = now + _net_ntp_hid_interval_ms;

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

  hid_device_t *device = hid_register(instance, "ntp", 0, hid_type_other,
                                      hid_class_sensor, 0, userdata,
                                      _net_ntp_hid_callbacks);
  if (device == NULL) {
    return NULL;
  }

  _net_ntp_hid_ntp = ntp;
  _net_ntp_hid_last_seconds = 0;
  _net_ntp_hid_interval_ms = (polling_interval_ms != 0)
                                ? polling_interval_ms
                                : NET_NTP_HID_DEFAULT_POLLING_INTERVAL_MS;
  _net_ntp_hid_next_attempt_ms = 0; // due immediately
  return device;
}
