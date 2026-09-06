#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>

// Joins a Wi-Fi network (if WIFI_SSID/WIFI_PASSWORD were supplied at build
// time - export them before running cmake, same as examples/wifi) and
// listens on ECHO_PORT. Every line typed at this process's own stdin is
// broadcast to every currently-connected client, and anything a client
// sends is printed with its address as a label - a basic two-way chat.
// Builds and runs on POSIX too (without a real Wi-Fi join) for testing
// against examples/echo/client without needing a Pico W on hand.
//
// Deliberately doesn't use app_flag_multicore: net_listener_init()'s
// callback and every sys_iostream_t op on a stream it hands out must run
// on the same thread/core that drives hw_poll() on Pico (see
// src/picofuse/net/pico/private.h's own doc) - on_event() can run on any
// worker once multicore is enabled, so this example stays single-core to
// keep that guarantee trivially true.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef ECHO_PORT
#define ECHO_PORT 7777
#endif
#define ECHO_MAX_CLIENTS 4

typedef struct {
  sys_iostream_t *conn;
  char label[40]; // "address:port", for _on_client_readable()'s printout
} _echo_client_t;

static sys_mutex_t *g_lock;
static _echo_client_t g_clients[ECHO_MAX_CLIENTS];
static net_listener_t *g_listener;

static void _broadcast(const char *buf, size_t n) {
  sys_mutex_lock(g_lock);
  for (size_t i = 0; i < ECHO_MAX_CLIENTS; i++) {
    if (g_clients[i].conn != NULL) {
      sys_iostream_write(g_clients[i].conn, buf, n);
    }
  }
  sys_mutex_unlock(g_lock);
}

// Prints whatever a client sends, labeled with its address - fires once
// per sys_iostream_read() worth of data, which (like examples/stdin's own
// callback) may be as little as a single keystroke rather than a whole
// line, since stdin on the client side is read the same raw, unbuffered
// way. A disconnected client is noticed the standard "readable, but
// read() returns 0" way (both net backends fire this callback one final
// time right when the peer goes away, specifically so read() returning 0
// here is unambiguous - see net/posix/socket.c's and net/pico/socket.c's
// own doc on their respective _net_conn_..._recv... paths).
static void _on_client_readable(sys_iostream_t *stream,
                                sys_iostream_event_t events, void *userdata) {
  if (!(events & sys_iostream_event_read)) {
    return;
  }
  _echo_client_t *client = (_echo_client_t *)userdata;
  char buf[128];
  size_t n = sys_iostream_read(stream, buf, sizeof(buf) - 1);
  if (n == 0) {
    sys_printf("[echo] client disconnected: %s\n", client->label);
    sys_iostream_close(stream);
    sys_mutex_lock(g_lock);
    client->conn = NULL;
    sys_mutex_unlock(g_lock);
    return;
  }
  buf[n] = '\0';
  sys_printf("[echo] %s: %s\n", client->label, buf);
}

static void _client_add(sys_iostream_t *conn, const net_addr_t *remote,
                        uint16_t remote_port) {
  sys_mutex_lock(g_lock);
  for (size_t i = 0; i < ECHO_MAX_CLIENTS; i++) {
    if (g_clients[i].conn == NULL) {
      g_clients[i].conn = conn;
      char addrbuf[32];
      net_addr_to_string(remote, addrbuf, sizeof(addrbuf));
      sys_sprintf(g_clients[i].label, sizeof(g_clients[i].label), "%s:%u",
                  addrbuf, (unsigned)remote_port);
      sys_mutex_unlock(g_lock);
      sys_iostream_set_callback(conn, _on_client_readable, &g_clients[i]);
      return;
    }
  }
  sys_mutex_unlock(g_lock);
  sys_printf("[echo] client list full (%d) - dropping connection\n",
             ECHO_MAX_CLIENTS);
  sys_iostream_close(conn);
}

static void _on_accept(net_listener_t *listener, sys_iostream_t *conn,
                       const net_addr_t *remote, uint16_t remote_port,
                       void *userdata) {
  (void)listener;
  (void)userdata;
  char addrbuf[32];
  net_addr_to_string(remote, addrbuf, sizeof(addrbuf));
  sys_printf("[echo] client connected: %s:%u\n", addrbuf,
             (unsigned)remote_port);
  _client_add(conn, remote, remote_port);
}

static void _on_stdin(sys_iostream_t *stream, sys_iostream_event_t events,
                      void *userdata) {
  (void)userdata;
  if (!(events & sys_iostream_event_read)) {
    return;
  }
  char buf[128];
  size_t n = sys_iostream_read(stream, buf, sizeof(buf));
  if (n > 0) {
    _broadcast(buf, n);
  }
}

static void _on_start(app_t *app, void *userdata) {
  (void)userdata;
  g_lock = sys_mutex_init();
  sys_assert(g_lock != NULL);

  hw_wifi_t *wifi = app_wifi(app);
  if (wifi != NULL && WIFI_SSID[0] != '\0') {
    hw_wifi_network_t network = {
        .ssid = WIFI_SSID,
        .auth = hw_wifi_auth_wpa2_aes,
    };
    sys_printf("[echo] joining ssid=%s\n", WIFI_SSID);
    if (!hw_wifi_connect(wifi, &network, WIFI_PASSWORD)) {
      sys_printf("[echo] failed to start connection\n");
    }
  } else if (wifi != NULL) {
    sys_printf("[echo] no WIFI_SSID set - listening without joining a "
               "network\n");
  }

  // Binding to "any" works immediately, whether or not (or not yet)
  // connected to a network - see net_listener_init()'s own doc.
  net_addr_t any = net_addr_v4_any();
  g_listener =
      net_listener_init(net_proto_tcp, &any, ECHO_PORT, _on_accept, NULL);
  if (g_listener == NULL) {
    sys_printf("[echo] failed to start listener on port %u\n", ECHO_PORT);
    app_shutdown(1);
    return;
  }
  sys_printf("[echo] listening on port %u - type to broadcast to clients\n",
             ECHO_PORT);

  if (!sys_iostream_set_callback(sys_stdin, _on_stdin, NULL)) {
    sys_printf("[echo] standard input callbacks are unavailable\n");
  }
}

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  (void)userdata;

  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event == NULL) {
    return;
  }

  switch (hid_event->type) {
  case hid_event_type_keycode:
    if (hid_event->data.keycode.keycode == KEYCODE_BUTTON_USER) {
      app_shutdown(0);
      sys_printf("[echo] shutting down\n");
    }
    break;
  case hid_event_type_wifi: {
    hw_wifi_event_t wifi_event = hid_event->data.wifi.event;
    if (wifi_event & hw_wifi_event_connected) {
      net_addr_t addr;
      if (hw_wifi_get_address(app_wifi(app), net_addr_family_v4, &addr)) {
        char addrbuf[32];
        net_addr_to_string(&addr, addrbuf, sizeof(addrbuf));
        sys_printf("[echo] wifi connected, address=%s\n", addrbuf);
      } else {
        sys_printf("[echo] wifi connected\n");
      }
      hw_led_set(app_led(app), 0, true);
    } else if (wifi_event & hw_wifi_event_disconnected) {
      sys_printf("[echo] wifi disconnected\n");
      hw_led_set(app_led(app), 0, false);
    } else if (wifi_event & hw_wifi_event_badauth) {
      sys_printf("[echo] wifi bad auth\n");
      hw_led_set(app_led(app), 0, false);
    } else if (wifi_event & hw_wifi_event_notfound) {
      sys_printf("[echo] wifi network not found\n");
      hw_led_set(app_led(app), 0, false);
    }
    break;
  }
  case hid_event_type_signal:
    sys_printf("[echo] received signal, shutting down\n");
    for (size_t i = 0; i < ECHO_MAX_CLIENTS; i++) {
      if (g_clients[i].conn != NULL) {
        sys_iostream_close(g_clients[i].conn);
        g_clients[i].conn = NULL;
      }
    }
    net_listener_deinit(g_listener);
    g_listener = NULL;
    sys_mutex_deinit(g_lock);
    g_lock = NULL;
    app_shutdown(0);
    break;
  default:
    break;
  }

  hid_event_free(hid_event);
}

int main(int argc, char *argv[]) {
  return app_main(argc, argv,
                  app_flag_wifi | app_flag_signal | app_flag_stdio_rtt |
                      app_flag_led | app_flag_user_button,
                  _on_start, _on_event, NULL);
}
