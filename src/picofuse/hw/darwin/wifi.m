#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stdio.h>
#include <string.h>

#import <CoreWLAN/CoreWLAN.h>
#import <Foundation/Foundation.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * Darwin's Wi-Fi backend is a singleton wrapping the default CWInterface -
 * see hw/wifi.h's own doc on hw_wifi_t for why there's no pool here. This
 * struct's definition is private to this file; the public header only ever
 * sees the opaque hw_wifi_t.
 */
struct hw_wifi_t {
  CWInterface *iface;
  hw_wifi_callback_t callback;
  void *userdata;
  bool active; ///< true between hw_wifi_init_client() and hw_wifi_deinit().
  bool busy;   ///< true while a scan or connect runs on a worker thread.
};

/**
 * Heap context for hw_wifi_connect()'s worker thread. Allocated with
 * sys_calloc() so the NSString field starts as a valid nil for ARC, and
 * explicitly niled before sys_free() releases the block - free() itself
 * does not run the ARC destructor for __strong fields.
 */
typedef struct {
  hw_wifi_t *wifi;
  hw_wifi_network_t network;
  NSString *password;
} _hw_wifi_connect_ctx_t;

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct hw_wifi_t _hw_wifi_adaptor;
static sys_mutex_t *_hw_wifi_mutex;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Notify the attached callback, if any - hw_wifi_set_callback()
 * means a handle may legitimately have none attached yet. */
static inline void _hw_wifi_notify(hw_wifi_t *wifi, hw_wifi_event_t event,
                                   const hw_wifi_network_t *network) {
  if (wifi->callback != NULL) {
    wifi->callback(wifi, event, network, wifi->userdata);
  }
}

static hw_wifi_auth_t _hw_wifi_auth_from_network(CWNetwork *net) {
  hw_wifi_auth_t auth = 0;
  if ([net supportsSecurity:kCWSecurityNone]) {
    auth |= hw_wifi_auth_open;
  }
  if ([net supportsSecurity:kCWSecurityWEP] ||
      [net supportsSecurity:kCWSecurityDynamicWEP]) {
    auth |= hw_wifi_auth_wep;
  }
  if ([net supportsSecurity:kCWSecurityWPAPersonal] ||
      [net supportsSecurity:kCWSecurityWPAPersonalMixed]) {
    // CoreWLAN only reports the security "type", not the cipher suite
    // (TKIP vs AES) separately - approximate as AES, the common case.
    auth |= hw_wifi_auth_wpa_aes;
  }
  if ([net supportsSecurity:kCWSecurityWPA2Personal] ||
      [net supportsSecurity:kCWSecurityPersonal]) {
    auth |= hw_wifi_auth_wpa2_aes;
  }
  if ([net supportsSecurity:kCWSecurityWPA3Personal] ||
      [net supportsSecurity:kCWSecurityWPA3Transition]) {
    auth |= hw_wifi_auth_wpa3_sae;
  }
  if ([net supportsSecurity:kCWSecurityWPAEnterprise] ||
      [net supportsSecurity:kCWSecurityWPAEnterpriseMixed] ||
      [net supportsSecurity:kCWSecurityWPA2Enterprise] ||
      [net supportsSecurity:kCWSecurityWPA3Enterprise] ||
      [net supportsSecurity:kCWSecurityEnterprise]) {
    auth |= hw_wifi_auth_enterprise;
  }
  return auth;
}

/**
 * Converts a scan result to the public hw_wifi_network_t shape. ssid/bssid
 * fields are only populated when Location Services is enabled and this
 * process is authorized to use it - see CWNetwork.h's own notes on -ssid/
 * -bssid. Without that authorization they're left zeroed, not guessed.
 */
static void _hw_wifi_network_from_cwnetwork(CWNetwork *net,
                                            hw_wifi_network_t *out) {
  memset(out, 0, sizeof(*out));

  NSString *ssid = net.ssid;
  if (ssid != nil) {
    strlcpy(out->ssid, ssid.UTF8String, sizeof(out->ssid));
  }

  NSString *bssid = net.bssid; // "XX:XX:XX:XX:XX:XX" or nil
  if (bssid != nil) {
    unsigned int b[6];
    if (sscanf(bssid.UTF8String, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2],
               &b[3], &b[4], &b[5]) == 6) {
      for (int i = 0; i < 6; i++) {
        out->bssid[i] = (uint8_t)b[i];
      }
    }
  }

  CWChannel *channel = net.wlanChannel;
  out->channel = channel != nil ? (uint8_t)channel.channelNumber : 0;
  out->rssi = (int16_t)net.rssiValue;
  out->auth = _hw_wifi_auth_from_network(net);
}

/**
 * Finds a scan result matching network->ssid (and network->bssid, if not
 * all-zero). CoreWLAN's -associateToNetwork: needs an actual CWNetwork
 * object, not just an SSID/password pair, so connecting always re-scans
 * for one first.
 */
static CWNetwork *_hw_wifi_find_network(CWInterface *iface,
                                        const hw_wifi_network_t *network) {
  NSString *ssidStr = [NSString stringWithUTF8String:network->ssid];
  NSData *ssidData = [ssidStr dataUsingEncoding:NSUTF8StringEncoding];
  NSSet<CWNetwork *> *found = [iface scanForNetworksWithSSID:ssidData
                                                        error:NULL];
  if (found == nil || found.count == 0) {
    return nil;
  }

  bool wantBSSID = false;
  for (int i = 0; i < 6; i++) {
    if (network->bssid[i] != 0) {
      wantBSSID = true;
      break;
    }
  }
  if (!wantBSSID) {
    return found.anyObject;
  }

  NSString *wantBSSIDStr = [NSString
      stringWithFormat:@"%02x:%02x:%02x:%02x:%02x:%02x", network->bssid[0],
                       network->bssid[1], network->bssid[2],
                       network->bssid[3], network->bssid[4],
                       network->bssid[5]];
  for (CWNetwork *net in found) {
    if ([net.bssid caseInsensitiveCompare:wantBSSIDStr] == NSOrderedSame) {
      return net;
    }
  }
  return nil;
}

static void _hw_wifi_scan_thread(void *arg) {
  hw_wifi_t *wifi = (hw_wifi_t *)arg;
  @autoreleasepool {
    NSError *error = nil;
    NSSet<CWNetwork *> *networks =
        [wifi->iface scanForNetworksWithSSID:nil error:&error];
    if (networks == nil) {
      sys_debugf("wifi", "scan failed: %s",
                 error.localizedDescription.UTF8String);
      _hw_wifi_notify(wifi, hw_wifi_event_error, NULL);
    } else {
      for (CWNetwork *net in networks) {
        hw_wifi_network_t result;
        _hw_wifi_network_from_cwnetwork(net, &result);
        _hw_wifi_notify(wifi, hw_wifi_event_scan, &result);
      }
      _hw_wifi_notify(wifi, hw_wifi_event_scan, NULL);
    }
  }
  sys_mutex_lock(_hw_wifi_mutex);
  wifi->busy = false;
  sys_mutex_unlock(_hw_wifi_mutex);
}

static void _hw_wifi_connect_thread(void *arg) {
  _hw_wifi_connect_ctx_t *ctx = (_hw_wifi_connect_ctx_t *)arg;
  hw_wifi_t *wifi = ctx->wifi;
  @autoreleasepool {
    _hw_wifi_notify(wifi, hw_wifi_event_joining, NULL);

    CWNetwork *target = _hw_wifi_find_network(wifi->iface, &ctx->network);
    if (target == nil) {
      _hw_wifi_notify(wifi, hw_wifi_event_notfound, NULL);
    } else {
      NSError *error = nil;
      if ([wifi->iface associateToNetwork:target
                                  password:ctx->password
                                     error:&error]) {
        hw_wifi_network_t result;
        _hw_wifi_network_from_cwnetwork(target, &result);
        _hw_wifi_notify(wifi, hw_wifi_event_connected, &result);
      } else {
        sys_debugf("wifi", "associate failed: %s",
                   error.localizedDescription.UTF8String);
        hw_wifi_event_t event;
        switch ((CWErr)error.code) {
        case kCWSupplicantTimeoutErr: // WPA/WPA2 handshake timed out
        case kCWChallengeFailureErr:  // WEP challenge rejected
        case kCWAssociationDeniedErr: // AP denied association outright
          event = hw_wifi_event_badauth;
          break;
        default:
          event = hw_wifi_event_error;
          break;
        }
        _hw_wifi_notify(wifi, event, NULL);
      }
    }
  }
  ctx->password = nil; // release the ARC-managed field before sys_free()
  sys_free(ctx);
  sys_mutex_lock(_hw_wifi_mutex);
  wifi->busy = false;
  sys_mutex_unlock(_hw_wifi_mutex);
}

///////////////////////////////////////////////////////////////////////////////
// MODULE LIFECYCLE

/** Called from hw_init(); see init.c. */
void _hw_wifi_module_init(void) {
  if (_hw_wifi_mutex == NULL) {
    _hw_wifi_mutex = sys_mutex_init();
  }
}

/** Called from hw_exit(); see init.c. */
void _hw_wifi_module_exit(void) {
  if (_hw_wifi_mutex != NULL) {
    sys_mutex_deinit(_hw_wifi_mutex);
    _hw_wifi_mutex = NULL;
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_wifi_t *hw_wifi_init_client(const char *country_code) {
  // macOS exposes no public API to set the Wi-Fi regulatory country code -
  // the system manages it based on physical location.
  (void)country_code;

  hw_wifi_t *wifi = NULL;
  @autoreleasepool {
    sys_mutex_lock(_hw_wifi_mutex);
    if (_hw_wifi_adaptor.active) {
      sys_mutex_unlock(_hw_wifi_mutex);
      return NULL;
    }

    CWInterface *iface = [CWWiFiClient sharedWiFiClient].interface;
    if (iface == nil) {
      sys_mutex_unlock(_hw_wifi_mutex);
      return NULL;
    }

    _hw_wifi_adaptor.iface = iface;
    _hw_wifi_adaptor.callback = NULL; // attached later via
                                      // hw_wifi_set_callback()
    _hw_wifi_adaptor.userdata = NULL;
    _hw_wifi_adaptor.active = true;
    _hw_wifi_adaptor.busy = false;
    wifi = &_hw_wifi_adaptor;
    sys_mutex_unlock(_hw_wifi_mutex);
  }
  return wifi;
}

hw_wifi_t *hw_wifi_init_accesspoint(const char *country_code,
                                    const char *ssid, const char *password,
                                    hw_wifi_auth_t auth) {
  // CoreWLAN's only host-a-network API is IBSS (ad-hoc) mode, via
  // -startIBSSModeWithSSID:security:channel:password:error: - deprecated in
  // macOS 11 and unsupported by modern Wi-Fi hardware/drivers. There is no
  // public API to run a real infrastructure access point from user space
  // on macOS; see hw/wifi.h's own doc: unsupported here.
  (void)country_code;
  (void)ssid;
  (void)password;
  (void)auth;
  return NULL;
}

hw_wifi_t *hw_wifi_init_device(const char *device) {
  // wpa_supplicant control sockets are a Linux concept; macOS manages Wi-Fi
  // through CoreWLAN instead - see hw_wifi_init_client().
  (void)device;
  return NULL;
}

void hw_wifi_set_callback(hw_wifi_t *wifi, hw_wifi_callback_t callback,
                          void *userdata) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor) {
    return;
  }
  sys_mutex_lock(_hw_wifi_mutex);
  if (wifi->active) {
    wifi->callback = callback;
    wifi->userdata = userdata;
  }
  sys_mutex_unlock(_hw_wifi_mutex);
}

void hw_wifi_deinit(hw_wifi_t *wifi) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor) {
    return;
  }
  @autoreleasepool {
    sys_mutex_lock(_hw_wifi_mutex);
    if (!_hw_wifi_adaptor.active) {
      sys_mutex_unlock(_hw_wifi_mutex);
      return;
    }
    // Note: this cannot interrupt a scan/connect already running on a
    // worker thread (see hw_wifi_scan()/hw_wifi_connect() below) -
    // CoreWLAN's scan/associate calls are synchronous with no cancellation
    // API, so a worker thread runs to completion regardless of active/busy
    // here. Deinitializing while busy is a caller error; any callback the
    // worker still delivers afterward refers to a handle the caller has
    // already released.
    [_hw_wifi_adaptor.iface disassociate];
    _hw_wifi_adaptor.iface = nil;
    _hw_wifi_adaptor.callback = NULL;
    _hw_wifi_adaptor.userdata = NULL;
    _hw_wifi_adaptor.active = false;
    _hw_wifi_adaptor.busy = false;
    sys_mutex_unlock(_hw_wifi_mutex);
  }
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_wifi_scan(hw_wifi_t *wifi) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor) {
    return false;
  }

  sys_mutex_lock(_hw_wifi_mutex);
  if (!wifi->active || wifi->busy) {
    sys_mutex_unlock(_hw_wifi_mutex);
    return false;
  }
  wifi->busy = true;
  sys_mutex_unlock(_hw_wifi_mutex);

  if (!sys_thread_create(_hw_wifi_scan_thread, wifi)) {
    sys_mutex_lock(_hw_wifi_mutex);
    wifi->busy = false;
    sys_mutex_unlock(_hw_wifi_mutex);
    return false;
  }
  return true;
}

bool hw_wifi_connect(hw_wifi_t *wifi, const hw_wifi_network_t *network,
                     const char *password) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor || network == NULL) {
    return false;
  }

  sys_mutex_lock(_hw_wifi_mutex);
  if (!wifi->active || wifi->busy) {
    sys_mutex_unlock(_hw_wifi_mutex);
    return false;
  }
  wifi->busy = true;
  sys_mutex_unlock(_hw_wifi_mutex);

  _hw_wifi_connect_ctx_t *ctx = sys_calloc(1, sizeof(*ctx));
  if (ctx == NULL) {
    sys_mutex_lock(_hw_wifi_mutex);
    wifi->busy = false;
    sys_mutex_unlock(_hw_wifi_mutex);
    return false;
  }
  ctx->wifi = wifi;
  ctx->network = *network;
  @autoreleasepool {
    ctx->password = (password != NULL && password[0] != '\0')
                        ? [NSString stringWithUTF8String:password]
                        : nil;
  }

  if (!sys_thread_create(_hw_wifi_connect_thread, ctx)) {
    ctx->password = nil;
    sys_free(ctx);
    sys_mutex_lock(_hw_wifi_mutex);
    wifi->busy = false;
    sys_mutex_unlock(_hw_wifi_mutex);
    return false;
  }
  return true;
}

bool hw_wifi_disconnect(hw_wifi_t *wifi) {
  if (wifi == NULL || wifi != &_hw_wifi_adaptor) {
    return false;
  }

  sys_mutex_lock(_hw_wifi_mutex);
  bool connected = wifi->active && wifi->iface.interfaceMode != kCWInterfaceModeNone;
  bool ok = wifi->active && (wifi->busy || connected);
  sys_mutex_unlock(_hw_wifi_mutex);
  if (!ok) {
    return false;
  }

  @autoreleasepool {
    // Note: if a scan/connect is currently busy, this can't actually abort
    // it (see hw_wifi_deinit()'s note) - it only disassociates from
    // whatever network is currently joined, if any.
    [wifi->iface disassociate];
  }
  _hw_wifi_notify(wifi, hw_wifi_event_disconnected, NULL);
  return true;
}
