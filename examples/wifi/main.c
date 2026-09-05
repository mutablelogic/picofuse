#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

static void _on_start(app_t *app, void *userdata) {
  (void)userdata;
  sys_debugf("wifi", "on_start: running on core %u of %u", sys_thread_core(),
             sys_thread_numcores());

  hw_wifi_t *wifi = app_wifi(app);
  if (wifi == NULL) {
    sys_debugf("wifi", "on_start: no Wi-Fi hardware on this platform/build");
    return;
  }

  // Join a network if WIFI_SSID/WIFI_PASSWORD were supplied at build time
  // (export them before running cmake), otherwise just scan for nearby
  // networks - either way, hid_register_wifi() (see app_main()) is already
  // observing wifi and will report the result as a hid_event_type_wifi
  // event below.
  if (WIFI_SSID[0] != '\0') {
    hw_wifi_network_t network = {
        .ssid = WIFI_SSID,
        .auth = hw_wifi_auth_wpa2_aes,
    };
    sys_debugf("wifi", "on_start: joining ssid=%s", WIFI_SSID);
    if (!hw_wifi_connect(wifi, &network, WIFI_PASSWORD)) {
      sys_debugf("wifi", "on_start: failed to start connection");
    }
  } else {
    sys_debugf("wifi", "on_start: scanning for nearby networks");
    if (!hw_wifi_scan(wifi)) {
      sys_debugf("wifi", "on_start: failed to start scan");
    }
  }
}

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  (void)userdata;

  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event == NULL) {
    return;
  }

  switch (hid_event->type) {
  case hid_event_type_wifi: {
    hw_wifi_event_t wifi_event = hid_event->data.wifi.event;

    if (wifi_event & hw_wifi_event_connected) {
      (void)hw_led_set(app_led(app), 0, true);
    } else if (wifi_event & hw_wifi_event_disconnected) {
      (void)hw_led_set(app_led(app), 0, false);
    }

    // label only matters for the debug log below - kept out of the LED
    // logic above so that block still compiles/behaves the same when
    // sys_debugf() itself compiles away to nothing under NDEBUG (see
    // sys/debugf.h), instead of leaving label write-only in that build.
#ifndef NDEBUG
    const char *label = "unknown";
    if (wifi_event & hw_wifi_event_scan) {
      label =
          hid_event->data.wifi.has_network ? "scan result" : "scan complete";
    } else if (wifi_event & hw_wifi_event_joining) {
      label = "joining";
    } else if (wifi_event & hw_wifi_event_connected) {
      label = "connected";
    } else if (wifi_event & hw_wifi_event_disconnected) {
      label = "disconnected";
    } else if (wifi_event & hw_wifi_event_badauth) {
      label = "bad auth";
    } else if (wifi_event & hw_wifi_event_notfound) {
      label = "not found";
    } else if (wifi_event & hw_wifi_event_error) {
      label = "error";
    } else if (wifi_event & hw_wifi_event_status) {
      label = "status";
    }

    if (hid_event->data.wifi.has_network) {
      sys_debugf("wifi", "on_event: wifi %s ssid=%s rssi=%d (core=%u)", label,
                 hid_event->data.wifi.network.ssid,
                 (int)hid_event->data.wifi.network.rssi, sys_thread_core());
    } else {
      sys_debugf("wifi", "on_event: wifi %s (core=%u)", label,
                 sys_thread_core());
    }
#endif
    break;
  }
  case hid_event_type_signal:
    // Ctrl-C/SIGTERM on a host build - a Pico board has no such signals.
    sys_debugf("wifi", "on_event: received signal, shutting down (core=%u)",
               sys_thread_core());
    app_shutdown(0);
    break;
  default:
    sys_debugf("wifi", "on_event: event type %d (core=%u)",
               (int)hid_event->type, sys_thread_core());
    break;
  }

  hid_event_free(hid_event);
}

int main(int argc, char *argv[]) {
  return app_main(argc, argv,
                  APP_FLAG_MULTICORE | APP_FLAG_WIFI | APP_FLAG_SIGNAL,
                  _on_start, _on_event, NULL);
}
