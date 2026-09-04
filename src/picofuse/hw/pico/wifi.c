#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>

#include "cyw43.h"
#include "cyw43_country.h"
#include <pico/cyw43_arch.h>

// Sentinel for hw_wifi_t.state meaning "no last-observed link state yet /
// force _hw_wifi_poll() to react to whatever cyw43_tcpip_link_status()
// next reports". Must never collide with a real CYW43_LINK_* value (the
// cyw43 SDK defines those in the range -3..3).
#define _HW_WIFI_STATE_UNKNOWN (-100)

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * Pico's Wi-Fi backend is a singleton wrapping the CYW43 chip's own STA/AP
 * state - see hw/wifi.h's own doc on hw_wifi_t for why there's no pool
 * here. This struct's definition is private to this file; the public
 * header only ever sees the opaque hw_wifi_t. This file is only compiled
 * when PICOFUSE_WIFI and PICO_CYW43_SUPPORTED both hold (see
 * CMakeLists.txt) - a board or build without real CYW43 hardware always
 * gets ../stub/wifi.c instead.
 */
struct hw_wifi_t {
  char country_code[3];
  hw_wifi_callback_t callback;
  void *userdata;
  sys_atomic_t flags;
  int state;
  hw_wifi_network_t network;
  uint64_t ts;
  bool active;      ///< true between a successful init and hw_wifi_deinit().
  bool accesspoint; ///< true if this handle came from hw_wifi_init_accesspoint().
};

/**
 * @brief Busy-operation flags, tracked independently of CYW43's own link
 * state so hw_wifi_scan()/hw_wifi_connect() can reject a second call while
 * one is already in flight.
 */
typedef enum {
  _hw_wifi_busy_scanning = (1 << 1),
  _hw_wifi_busy_joining = (1 << 2),
  _hw_wifi_busy_leaving = (1 << 3),
} _hw_wifi_busy_flags_t;

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct hw_wifi_t _hw_wifi_adaptor = {0};
static const char _hw_wifi_default_country_code[] = "XX";
static uint32_t _hw_wifi_status_interval_ms = (1000 * 60);

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static inline int _hw_wifi_link_status(void) {
  int status;
  cyw43_arch_lwip_begin();
  status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  return status;
}

/** @brief Return true if the STA link is up. */
static inline bool _hw_wifi_up(hw_wifi_t *wifi) {
  (void)wifi;
  return _hw_wifi_link_status() == CYW43_LINK_UP;
}

/** @brief Return true if busy (leaving, joining, scanning). */
static inline bool _hw_wifi_get_busy(hw_wifi_t *wifi,
                                     _hw_wifi_busy_flags_t flags) {
  return (sys_atomic_get(&wifi->flags) & flags) != 0;
}

/** @brief Set/clear busy flags. */
static inline void _hw_wifi_set_busy(hw_wifi_t *wifi,
                                     _hw_wifi_busy_flags_t flags, bool busy) {
  if (busy) {
    sys_atomic_set_bits(&wifi->flags, flags);
  } else {
    sys_atomic_clear_bits(&wifi->flags, flags);
  }
}

/** @brief Return country code as uint32_t for the SDK from a string. */
static uint32_t _hw_wifi_country_code(const char *country_code);

/**
 * @brief Map a single hw_wifi_auth_t value to a CYW43 AP auth constant.
 * @return false if this auth mode has no access-point equivalent (WEP,
 * WPA3-SAE, enterprise, or a multi-bit combination) - see
 * cyw43_arch_enable_ap_mode()'s own doc: only open, WPA-TKIP-PSK,
 * WPA2-AES-PSK, or WPA2-mixed-PSK are supported for AP mode.
 */
static bool _hw_wifi_ap_auth(hw_wifi_auth_t auth, uint32_t *out);

/** @brief Forward declare the scanning callback. */
static int _hw_wifi_scan_callback(void *env,
                                  const cyw43_ev_scan_result_t *result);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_wifi_t *hw_wifi_init_client(const char *country_code,
                               hw_wifi_callback_t callback, void *userdata) {
  sys_debugf("wifi", "wifi_init_client: country_code=%s callback=%p userdata=%p",
             country_code != NULL ? country_code : "(null)", (void *)callback,
             userdata);
  if (callback == NULL || _hw_wifi_adaptor.active) {
    return NULL;
  }

  if (country_code == NULL) {
    country_code = _hw_wifi_default_country_code;
  }
  if (strlen(country_code) != 2u) {
    return NULL;
  }
  if (!cyw43_is_initialized(&cyw43_state)) {
    return NULL;
  }

  memcpy(_hw_wifi_adaptor.country_code, country_code, 2);
  _hw_wifi_adaptor.country_code[2] = '\0';
  _hw_wifi_adaptor.callback = callback;
  _hw_wifi_adaptor.userdata = userdata;
  _hw_wifi_adaptor.accesspoint = false;
  _hw_wifi_adaptor.state = _HW_WIFI_STATE_UNKNOWN;
  sys_atomic_init(&_hw_wifi_adaptor.flags, 0);
  _hw_wifi_adaptor.active = true;

  return &_hw_wifi_adaptor;
}

hw_wifi_t *hw_wifi_init_accesspoint(const char *country_code,
                                    const char *ssid, const char *password,
                                    hw_wifi_auth_t auth,
                                    hw_wifi_callback_t callback,
                                    void *userdata) {
  sys_debugf("wifi", "wifi_init_accesspoint: ssid=%s auth=%u",
             ssid != NULL ? ssid : "(null)", (unsigned)auth);
  if (callback == NULL || _hw_wifi_adaptor.active || ssid == NULL) {
    return NULL;
  }

  size_t ssid_len = strlen(ssid);
  if (ssid_len == 0 || ssid_len > HW_WIFI_SSID_MAX_LENGTH) {
    return NULL;
  }

  uint32_t cyw43_auth;
  if (!_hw_wifi_ap_auth(auth, &cyw43_auth)) {
    return NULL;
  }

  size_t password_len = password != NULL ? strlen(password) : 0;
  if (cyw43_auth != CYW43_AUTH_OPEN && password_len == 0) {
    return NULL;
  }

  if (country_code == NULL) {
    country_code = _hw_wifi_default_country_code;
  }
  if (strlen(country_code) != 2u) {
    return NULL;
  }
  if (!cyw43_is_initialized(&cyw43_state)) {
    return NULL;
  }

  memcpy(_hw_wifi_adaptor.country_code, country_code, 2);
  _hw_wifi_adaptor.country_code[2] = '\0';
  _hw_wifi_adaptor.callback = callback;
  _hw_wifi_adaptor.userdata = userdata;
  _hw_wifi_adaptor.accesspoint = true;
  _hw_wifi_adaptor.state = _HW_WIFI_STATE_UNKNOWN;
  sys_atomic_init(&_hw_wifi_adaptor.flags, 0);

  // Mirrors cyw43_arch_enable_ap_mode()'s own body, substituting our
  // per-call country code for its hardcoded process-wide default - the
  // same reason hw_wifi_init_client() above calls cyw43_wifi_set_up()
  // directly rather than cyw43_arch_enable_sta_mode().
  cyw43_arch_lwip_begin();
  cyw43_wifi_ap_set_ssid(&cyw43_state, ssid_len, (const uint8_t *)ssid);
  if (password_len > 0) {
    cyw43_wifi_ap_set_password(&cyw43_state, password_len,
                               (const uint8_t *)password);
  }
  cyw43_wifi_ap_set_auth(&cyw43_state, cyw43_auth);
  cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_AP, true,
                    _hw_wifi_country_code(_hw_wifi_adaptor.country_code));
  cyw43_arch_lwip_end();

  _hw_wifi_adaptor.active = true;
  return &_hw_wifi_adaptor;
}

/** @brief Stub function - wpa_supplicant control sockets are a Linux concept. */
hw_wifi_t *hw_wifi_init_device(const char *device, hw_wifi_callback_t callback,
                               void *userdata) {
  sys_debugf("wifi", "wifi_init_device: device=%s callback=%p userdata=%p",
             device != NULL ? device : "(null)", (void *)callback, userdata);
  (void)device;
  (void)callback;
  (void)userdata;
  return NULL;
}

void hw_wifi_deinit(hw_wifi_t *wifi) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor || !_hw_wifi_adaptor.active) {
    return;
  }

  sys_debugf("wifi", "wifi_deinit: wifi=%p", (void *)wifi);

  if (cyw43_is_initialized(&cyw43_state)) {
    cyw43_arch_lwip_begin();
    if (wifi->accesspoint) {
      cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_AP, false,
                        _hw_wifi_country_code(wifi->country_code));
    } else {
      // Stop any in-flight connect/scan activity and disconnect from STA.
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
      cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_STA, false,
                        _hw_wifi_country_code(wifi->country_code));
    }
    cyw43_arch_lwip_end();
  }

  memset(wifi, 0, sizeof(struct hw_wifi_t));
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Begin an asynchronous scan for nearby Wi-Fi networks.
 */
bool hw_wifi_scan(hw_wifi_t *wifi) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor || !wifi->active ||
      wifi->accesspoint) {
    return false;
  }

  // If we're already leaving, joining or scanning, don't start a new scan.
  if (_hw_wifi_get_busy(wifi, _hw_wifi_busy_leaving | _hw_wifi_busy_joining |
                                  _hw_wifi_busy_scanning)) {
    return false;
  }

  if (!_hw_wifi_up(wifi)) {
    // Bring Wi-Fi up in STA (client) mode.
    cyw43_arch_lwip_begin();
    cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_STA, true,
                      _hw_wifi_country_code(wifi->country_code));
    cyw43_arch_lwip_end();
  }

  // Pass the wifi handle as the callback environment.
  cyw43_wifi_scan_options_t opts = {0};
  cyw43_arch_lwip_begin();
  int scan_result =
      cyw43_wifi_scan(&cyw43_state, &opts, wifi, _hw_wifi_scan_callback);
  cyw43_arch_lwip_end();
  if (scan_result != 0) {
    return false;
  }

  _hw_wifi_set_busy(wifi, _hw_wifi_busy_scanning, true);
  wifi->state = _HW_WIFI_STATE_UNKNOWN;
  return true;
}

/**
 * @brief Begin an asynchronous connection to a Wi-Fi network.
 */
bool hw_wifi_connect(hw_wifi_t *wifi, const hw_wifi_network_t *network,
                     const char *password) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor || !wifi->active ||
      wifi->accesspoint || network == NULL) {
    return false;
  }

  // If we're already leaving, joining or scanning, don't start a new join.
  if (_hw_wifi_get_busy(wifi, _hw_wifi_busy_leaving | _hw_wifi_busy_joining |
                                  _hw_wifi_busy_scanning)) {
    return false;
  }

  size_t ssid_len = strlen(network->ssid);
  if (ssid_len == 0 || ssid_len > HW_WIFI_SSID_MAX_LENGTH) {
    return false;
  }

  const char *key = password != NULL ? password : "";
  size_t key_len = strlen(key);

  uint32_t auth = CYW43_AUTH_OPEN;
  if ((network->auth & hw_wifi_auth_wpa3_sae) != 0) {
#if defined(CYW43_AUTH_WPA3_SAE_AES_PSK)
    auth = CYW43_AUTH_WPA3_SAE_AES_PSK;
#else
    auth = CYW43_AUTH_WPA2_AES_PSK;
#endif
  } else if ((network->auth & (hw_wifi_auth_wpa2_aes | hw_wifi_auth_wpa2_tkip |
                               hw_wifi_auth_wpa_aes)) != 0) {
    auth = CYW43_AUTH_WPA2_AES_PSK;
  } else if ((network->auth & hw_wifi_auth_wpa_tkip) != 0) {
    auth = CYW43_AUTH_WPA_TKIP_PSK;
  }

  if (auth != CYW43_AUTH_OPEN && key_len == 0) {
    return false;
  }

  if (!_hw_wifi_up(wifi)) {
    cyw43_arch_lwip_begin();
    cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_STA, true,
                      _hw_wifi_country_code(wifi->country_code));
    cyw43_arch_lwip_end();
  }

  // Reset prior connection state and store the requested network.
  memset(&wifi->network, 0, sizeof(wifi->network));
  memcpy(&wifi->network, network, sizeof(wifi->network));
  wifi->state = _HW_WIFI_STATE_UNKNOWN;
  wifi->ts = 0;

  cyw43_arch_lwip_begin();
  int join_result = cyw43_wifi_join(
      &cyw43_state, ssid_len, (const uint8_t *)wifi->network.ssid, key_len,
      (const uint8_t *)key, auth, NULL, 0);
  cyw43_arch_lwip_end();

  if (join_result != 0) {
    memset(&wifi->network, 0, sizeof(wifi->network));
    return false;
  }

  _hw_wifi_set_busy(wifi, _hw_wifi_busy_joining, true);
  return true;
}

/**
 * @brief Disconnect from a previously-connected Wi-Fi network.
 */
bool hw_wifi_disconnect(hw_wifi_t *wifi) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor || !wifi->active ||
      wifi->accesspoint || !cyw43_is_initialized(&cyw43_state)) {
    return false;
  }

  // If scanning or joining is in progress, abort it - this is itself the
  // "disconnect" per hw/wifi.h's own doc ("aborts an in-progress
  // connection attempt or scan"), so report it as initiated.
  if (_hw_wifi_get_busy(wifi, _hw_wifi_busy_scanning | _hw_wifi_busy_joining)) {
    cyw43_arch_lwip_begin();
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_end();
    _hw_wifi_set_busy(wifi, _hw_wifi_busy_scanning | _hw_wifi_busy_joining,
                      false);
    wifi->state = _HW_WIFI_STATE_UNKNOWN;
    wifi->ts = 0;
    memset(&wifi->network, 0, sizeof(wifi->network));
    return true;
  }

  int state = _hw_wifi_link_status();
  if (state != CYW43_LINK_JOIN && state != CYW43_LINK_NOIP &&
      state != CYW43_LINK_UP) {
    return false;
  }

  cyw43_arch_lwip_begin();
  int leave_result = cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  if (leave_result != 0) {
    return false;
  }

  _hw_wifi_set_busy(wifi, _hw_wifi_busy_leaving, true);
  wifi->state = _HW_WIFI_STATE_UNKNOWN;
  wifi->ts = 0;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Get country code as uint32_t for the SDK from a Wi-Fi handle. */
static uint32_t _hw_wifi_country_code(const char *country_code) {
  if (country_code == NULL || strlen(country_code) != 2u) {
    return 0;
  } else {
    return CYW43_COUNTRY(country_code[0], country_code[1], 0);
  }
}

static bool _hw_wifi_ap_auth(hw_wifi_auth_t auth, uint32_t *out) {
  switch (auth) {
  case hw_wifi_auth_open:
    *out = CYW43_AUTH_OPEN;
    return true;
  case hw_wifi_auth_wpa_tkip:
    *out = CYW43_AUTH_WPA_TKIP_PSK;
    return true;
  case hw_wifi_auth_wpa_aes:
  case hw_wifi_auth_wpa2_aes:
    *out = CYW43_AUTH_WPA2_AES_PSK;
    return true;
  case hw_wifi_auth_wpa2_tkip:
    *out = CYW43_AUTH_WPA2_MIXED_PSK;
    return true;
  default:
    return false;
  }
}

/** @brief Get current Wi-Fi channel for connected Wi-Fi. */
static uint8_t _hw_wifi_get_channel(hw_wifi_t *wifi) {
  sys_assert(wifi == &_hw_wifi_adaptor);
  uint32_t channel = 0;
  cyw43_arch_lwip_begin();
  cyw43_ioctl(&cyw43_state, CYW43_IOCTL_GET_CHANNEL, sizeof(channel),
              (uint8_t *)&channel, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  return (uint8_t)channel;
}

/** @brief Get bssid for connected Wi-Fi. */
static void _hw_wifi_get_bssid(hw_wifi_t *wifi, uint8_t bssid[6]) {
  sys_assert(wifi == &_hw_wifi_adaptor);
  sys_assert(bssid != NULL);
  memset(bssid, 0, 6);
  cyw43_arch_lwip_begin();
  cyw43_wifi_get_bssid(&cyw43_state, bssid);
  cyw43_arch_lwip_end();
}

/** @brief Get signal strength for connected Wi-Fi. */
static int16_t _hw_wifi_get_rssi(hw_wifi_t *wifi) {
  sys_assert(wifi == &_hw_wifi_adaptor);
  int32_t rssi = 0;
  cyw43_arch_lwip_begin();
  int rssi_result = cyw43_wifi_get_rssi(&cyw43_state, &rssi);
  cyw43_arch_lwip_end();
  if (rssi_result == 0) {
    return (int16_t)rssi;
  }
  return 0;
}

/** @brief Callback for scan results from the CYW43 driver. */
static int _hw_wifi_scan_callback(void *ctx,
                                  const cyw43_ev_scan_result_t *result) {
  static hw_wifi_network_t network = {0};
  hw_wifi_t *wifi = (hw_wifi_t *)ctx;
  sys_assert(wifi == &_hw_wifi_adaptor);

  // Stop scanning if the callback was cleared.
  if (wifi->callback == NULL) {
    return -1;
  }

  // Driver may invoke with result == NULL to indicate completion; ignored
  // here since _hw_wifi_poll() notifies completion once scanning goes
  // inactive.
  if (result == NULL) {
    return 0;
  }

  size_t ssid_len = result->ssid_len;
  if (ssid_len >= sizeof(network.ssid)) {
    ssid_len = sizeof(network.ssid) - 1;
  }
  memcpy(network.ssid, result->ssid, ssid_len);
  network.ssid[ssid_len] = '\0';

  sys_assert(sizeof(network.bssid) == sizeof(result->bssid));
  memcpy(network.bssid, result->bssid, sizeof(network.bssid));

  network.channel = result->channel;
  network.rssi = (int16_t)result->rssi;

  network.auth = 0;
  uint8_t am = result->auth_mode;
  if (am == (uint8_t)CYW43_AUTH_OPEN) {
    network.auth = hw_wifi_auth_open;
  } else if (am == (uint8_t)CYW43_AUTH_WPA_TKIP_PSK) {
    network.auth = hw_wifi_auth_wpa_tkip;
  } else if (am == (uint8_t)CYW43_AUTH_WPA2_AES_PSK ||
             am == (uint8_t)CYW43_AUTH_WPA2_MIXED_PSK) {
    network.auth = hw_wifi_auth_wpa2_aes;
  }
#if defined(CYW43_AUTH_WPA3_SAE_AES_PSK)
  else if (am == (uint8_t)CYW43_AUTH_WPA3_SAE_AES_PSK) {
    network.auth = hw_wifi_auth_wpa3_sae;
  }
#elif defined(CYW43_AUTH_WPA3_SAE_PSK)
  else if (am == (uint8_t)CYW43_AUTH_WPA3_SAE_PSK) {
    network.auth = hw_wifi_auth_wpa3_sae;
  }
#endif

  wifi->callback(wifi, hw_wifi_event_scan, &network, wifi->userdata);

  // Continue scanning.
  return 0;
}

/**
 * @brief Poll the Wi-Fi state and handle events. Called from hw_poll();
 * see init.c.
 */
void _hw_wifi_poll(void) {
  hw_wifi_t *wifi = &_hw_wifi_adaptor;
  if (!wifi->active) {
    return;
  }

  // Per-station join/leave on an access point isn't exposed here. A live
  // AP-side signal does exist in principle - the driver's CYW43_EV_LINK
  // handler (cyw43_ctrl.c) calls netif_set_link_up()/_down() for
  // CYW43_ITF_AP, which cyw43_tcpip_link_status() would report back as
  // CYW43_LINK_UP/_DOWN - but reading it here via the documented
  // cyw43_arch_lwip_begin()/_end() pattern was confirmed on real hardware
  // to deadlock the *next* cyw43_arch_poll() call, every time, even with
  // no station ever attempting to join. Root cause not identified (likely
  // an async_context/lock interaction inside the vendored SDK, not
  // something wrong with the lock usage itself - every other call in this
  // file uses the identical pattern without issue). Left as a stub pending
  // further investigation - do not re-add this without confirming the
  // hang is understood and fixed.
  if (wifi->accesspoint) {
    return;
  }

  // If the timestamp field is > 0 then report occasionally on connection
  // status.
  if (wifi->ts > 0) {
    uint64_t now = sys_timestamp_ms();
    if (now - wifi->ts > _hw_wifi_status_interval_ms) {
      wifi->ts = now;
      if (_hw_wifi_up(wifi)) {
        wifi->network.rssi = _hw_wifi_get_rssi(wifi);
        _hw_wifi_get_bssid(wifi, wifi->network.bssid);
        wifi->network.channel = _hw_wifi_get_channel(wifi);

        // A periodic refresh, not a new connection; see
        // hw_wifi_event_connected below for the one-time join transition.
        wifi->callback(wifi, hw_wifi_event_status, &wifi->network,
                       wifi->userdata);
      }
    }
  }

  // If we're not joining, leaving or scanning, there's nothing to poll.
  if (!_hw_wifi_get_busy(wifi, _hw_wifi_busy_leaving | _hw_wifi_busy_joining |
                                   _hw_wifi_busy_scanning)) {
    return;
  }

  // If we're scanning and scan becomes inactive, end the scan.
  if (_hw_wifi_get_busy(wifi, _hw_wifi_busy_scanning)) {
    bool scan_active;
    cyw43_arch_lwip_begin();
    scan_active = cyw43_wifi_scan_active(&cyw43_state);
    cyw43_arch_lwip_end();
    if (!scan_active) {
      _hw_wifi_set_busy(wifi, _hw_wifi_busy_scanning, false);
      wifi->callback(wifi, hw_wifi_event_scan, NULL, wifi->userdata);
    }
    return;
  }

  // Get current link state, and act if it's changed.
  int state = _hw_wifi_link_status();
  if (state == wifi->state) {
    return;
  }
  wifi->state = state;

  switch (state) {
  case CYW43_LINK_DOWN:
    sys_debugf("wifi", "CYW43_LINK_DOWN");
    _hw_wifi_set_busy(wifi,
                      _hw_wifi_busy_joining | _hw_wifi_busy_leaving |
                          _hw_wifi_busy_scanning,
                      false);
    wifi->state = _HW_WIFI_STATE_UNKNOWN;
    wifi->callback(wifi, hw_wifi_event_disconnected, NULL, wifi->userdata);
    memset(&wifi->network, 0, sizeof(wifi->network));
    break;
  case CYW43_LINK_JOIN:
    sys_debugf("wifi", "CYW43_LINK_JOIN");
    _hw_wifi_set_busy(wifi, _hw_wifi_busy_joining, true);
    wifi->callback(wifi, hw_wifi_event_joining, &wifi->network, wifi->userdata);
    break;
  case CYW43_LINK_NOIP:
    sys_debugf("wifi", "CYW43_LINK_NOIP");
    break;
  case CYW43_LINK_UP:
    sys_debugf("wifi", "CYW43_LINK_UP");
    if (_hw_wifi_get_busy(wifi, _hw_wifi_busy_joining)) {
      _hw_wifi_set_busy(wifi, _hw_wifi_busy_joining, false);
      wifi->state = _HW_WIFI_STATE_UNKNOWN;

      wifi->network.rssi = _hw_wifi_get_rssi(wifi);
      _hw_wifi_get_bssid(wifi, wifi->network.bssid);
      wifi->network.channel = _hw_wifi_get_channel(wifi);
      wifi->ts = sys_timestamp_ms();

      wifi->callback(wifi, hw_wifi_event_connected, &wifi->network,
                     wifi->userdata);
    }
    break;
  case CYW43_LINK_FAIL:
    sys_debugf("wifi", "CYW43_LINK_FAIL");
    _hw_wifi_set_busy(wifi, _hw_wifi_busy_joining, false);
    wifi->state = _HW_WIFI_STATE_UNKNOWN;
    wifi->callback(wifi, hw_wifi_event_error, &wifi->network, wifi->userdata);
    memset(&wifi->network, 0, sizeof(wifi->network));
    break;
  case CYW43_LINK_NONET:
    sys_debugf("wifi", "CYW43_LINK_NONET");
    _hw_wifi_set_busy(wifi, _hw_wifi_busy_joining, false);
    wifi->state = _HW_WIFI_STATE_UNKNOWN;
    wifi->callback(wifi, hw_wifi_event_notfound, &wifi->network,
                   wifi->userdata);
    memset(&wifi->network, 0, sizeof(wifi->network));
    break;
  case CYW43_LINK_BADAUTH:
    sys_debugf("wifi", "CYW43_LINK_BADAUTH");
    _hw_wifi_set_busy(wifi, _hw_wifi_busy_joining, false);
    wifi->state = _HW_WIFI_STATE_UNKNOWN;
    wifi->callback(wifi, hw_wifi_event_badauth, &wifi->network, wifi->userdata);
    memset(&wifi->network, 0, sizeof(wifi->network));
    break;
  default:
    sys_debugf("wifi", "CYW43_LINK_UNKNOWN");
    _hw_wifi_set_busy(wifi,
                      _hw_wifi_busy_joining | _hw_wifi_busy_leaving |
                          _hw_wifi_busy_scanning,
                      false);
    wifi->state = _HW_WIFI_STATE_UNKNOWN;
    wifi->callback(wifi, hw_wifi_event_error, &wifi->network, wifi->userdata);
    memset(&wifi->network, 0, sizeof(wifi->network));
    break;
  }
}
