#include <picofuse/hw.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no Wi-Fi hardware on this platform. */
hw_wifi_t *hw_wifi_init_client(const char *country_code,
                               hw_wifi_callback_t callback, void *userdata) {
  (void)country_code;
  (void)callback;
  (void)userdata;
  return NULL;
}

/** Stub implementation: no Wi-Fi hardware on this platform. */
hw_wifi_t *hw_wifi_init_accesspoint(const char *country_code,
                                    const char *ssid, const char *password,
                                    hw_wifi_auth_t auth,
                                    hw_wifi_callback_t callback,
                                    void *userdata) {
  (void)country_code;
  (void)ssid;
  (void)password;
  (void)auth;
  (void)callback;
  (void)userdata;
  return NULL;
}

/** Stub implementation: no Wi-Fi hardware on this platform. */
hw_wifi_t *hw_wifi_init_device(const char *device, hw_wifi_callback_t callback,
                               void *userdata) {
  (void)device;
  (void)callback;
  (void)userdata;
  return NULL;
}

/** Stub implementation: no Wi-Fi hardware on this platform. */
void hw_wifi_deinit(hw_wifi_t *wifi) { (void)wifi; }

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** Stub implementation: no Wi-Fi hardware on this platform. */
bool hw_wifi_scan(hw_wifi_t *wifi) {
  (void)wifi;
  return false;
}

/** Stub implementation: no Wi-Fi hardware on this platform. */
bool hw_wifi_connect(hw_wifi_t *wifi, const hw_wifi_network_t *network,
                     const char *password) {
  (void)wifi;
  (void)network;
  (void)password;
  return false;
}

/** Stub implementation: no Wi-Fi hardware on this platform. */
bool hw_wifi_disconnect(hw_wifi_t *wifi) {
  (void)wifi;
  return false;
}
