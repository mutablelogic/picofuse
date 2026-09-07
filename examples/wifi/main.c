#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Wi-Fi is application-managed for now, not app_main()'s job - own the
// handle here, bring it up in on_start(), register it with HID so its
// status flows in as hid_event_type_wifi events, and bring it down again
// on shutdown.
static hw_wifi_t *g_wifi = NULL;

// Started once Wi-Fi actually connects (see _on_event() below) - Pico has
// no battery-backed RTC, so without syncing over NTP its clock just keeps
// counting from zero at every power-on/reset.
static net_ntp_t *g_ntp = NULL;
static hid_device_t *g_ntp_device = NULL;

static void _on_start(app_t *app, void *userdata) {
  (void)userdata;
  sys_debugf("wifi", "on_start: running on core %u of %u", sys_thread_core(),
             sys_thread_numcores());

  g_wifi = hw_wifi_init_client("XX");
  if (g_wifi == NULL) {
    sys_debugf("wifi", "on_start: no Wi-Fi hardware on this platform/build");
    return;
  }
  if (hid_register_wifi(app_hid(app), g_wifi, NULL) == NULL) {
    sys_debugf("wifi", "on_start: hid_register_wifi failed");
    hw_wifi_deinit(g_wifi);
    g_wifi = NULL;
    return;
  }

  // Join a network if WIFI_SSID/WIFI_PASSWORD were supplied at build time
  // (export them before running cmake), otherwise just scan for nearby
  // networks - either way, the hid_register_wifi() call above is already
  // observing wifi and will report the result as a hid_event_type_wifi
  // event below.
  if (WIFI_SSID[0] != '\0') {
    hw_wifi_network_t network = {
        .ssid = WIFI_SSID,
        .auth = hw_wifi_auth_wpa2_aes,
    };
    sys_debugf("wifi", "on_start: joining ssid=%s", WIFI_SSID);
    if (!hw_wifi_connect(g_wifi, &network, WIFI_PASSWORD)) {
      sys_debugf("wifi", "on_start: failed to start connection");
    }
  } else {
    sys_debugf("wifi", "on_start: scanning for nearby networks");
    if (!hw_wifi_scan(g_wifi)) {
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

      // Start syncing the system clock over NTP now that there's a
      // network route - only once, the first time we connect; the
      // periodic poll this registers keeps re-syncing from here on its
      // own, tolerating any later, temporary disconnection just fine.
      if (g_ntp == NULL) {
        g_ntp = net_ntp_init(NULL, 0, 3000);
        if (g_ntp != NULL) {
          g_ntp_device = net_ntp_register_hid(app_hid(app), g_ntp, 0, NULL);
          if (g_ntp_device == NULL) {
            sys_debugf("wifi", "on_event: net_ntp_register_hid failed");
            net_ntp_deinit(g_ntp);
            g_ntp = NULL;
          }
        } else {
          sys_debugf("wifi", "on_event: net_ntp_init failed");
        }
      }
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
  case hid_event_type_time: {
    sys_date_t current = {0};
    if (sys_date_get_now(&current) &&
        current.seconds == hid_event->data.time.date.seconds) {
      break; // clock's already right - nothing to change
    }
    if (sys_date_set_now(&hid_event->data.time.date)) {
      char date_buf[32];
      sys_date_to_string(&hid_event->data.time.date, sys_date_format_iso8601,
                         date_buf, sizeof(date_buf));
      sys_printf("[wifi] system clock synced via NTP: %s\n", date_buf);
    } else {
      sys_debugf("wifi", "on_event: sys_date_set_now failed (core=%u)",
                 sys_thread_core());
    }
    break;
  }
  case hid_event_type_signal:
    // Ctrl-C/SIGTERM on a host build - a Pico board has no such signals.
    sys_debugf("wifi", "on_event: received signal, shutting down (core=%u)",
               sys_thread_core());
    if (g_ntp_device != NULL) {
      hid_deregister(app_hid(app), g_ntp_device);
      g_ntp_device = NULL;
    }
    if (g_ntp != NULL) {
      net_ntp_deinit(g_ntp);
      g_ntp = NULL;
    }
    if (g_wifi != NULL) {
      hw_wifi_deinit(g_wifi);
      g_wifi = NULL;
    }
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
                  app_flag_signal | app_flag_led | app_flag_stdio_rtt,
                  _on_start, _on_event, NULL);
}
