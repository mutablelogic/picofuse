#pragma once
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// Shared Wi-Fi join/leave scaffolding for Pico-only real-hardware tests
// that need a real network route before doing their own protocol work -
// factored out of net_010/main.c, the first test to need it. Requires
// WIFI_SSID/WIFI_PASSWORD as compile definitions (see test/CMakeLists.txt).
//
// test_wifi_join()/test_wifi_leave() are no-ops on a host build,
// regardless of PICOFUSE_WIFI - a host already has whatever network
// route it needs (true even with a real Wi-Fi backend enabled there,
// e.g. Darwin's CoreWLAN), and must never scan/join the machine's own
// real Wi-Fi network as a side effect of running a test.

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifdef SYSTEM_NAME_PICO

#define TEST_WIFI_TIMEOUT_MS (30 * 1000)
#define TEST_WIFI_POLL_MS 100

typedef struct {
  hw_wifi_network_t found_network;
  bool found;
  bool scan_done;
  volatile hw_wifi_event_t last_connect_event;
} _test_wifi_state_t;

static _test_wifi_state_t _test_wifi_state;

static void _test_wifi_on_event(hw_wifi_t *wifi, hw_wifi_event_t event,
                                const hw_wifi_network_t *network,
                                void *userdata) {
  (void)wifi;
  (void)userdata;
  switch (event) {
  case hw_wifi_event_scan:
    if (network == NULL) {
      _test_wifi_state.scan_done = true;
    } else if (strcmp(network->ssid, WIFI_SSID) == 0) {
      _test_wifi_state.found_network = *network;
      _test_wifi_state.found = true;
    }
    break;
  case hw_wifi_event_connected:
  case hw_wifi_event_disconnected:
  case hw_wifi_event_notfound:
  case hw_wifi_event_badauth:
  case hw_wifi_event_error:
    _test_wifi_state.last_connect_event = event;
    break;
  default:
    break;
  }
}

static bool _test_wifi_wait(volatile hw_wifi_event_t *slot,
                            uint64_t timeout_ms) {
  uint64_t start = sys_timestamp_ms();
  while (*slot == 0 && sys_timestamp_ms() - start < timeout_ms) {
    hw_poll();
    sys_sleep_ms(TEST_WIFI_POLL_MS);
  }
  return *slot != 0;
}

#endif // SYSTEM_NAME_PICO

/** @brief Scan for WIFI_SSID and join it.
 * @param tag Printed as "[tag] ..." in progress/skip messages.
 * @return Connected handle, or NULL if WIFI_SSID is unset, this isn't a
 * Pico build (see this file's own top comment), or there's no Wi-Fi
 * backend on this board (all printed, not asserted - those are "not
 * applicable here", not bugs). Asserts on a scan/connect that fails
 * outright once WIFI_SSID is actually set, same as net_010's own
 * reasoning.
 */
static hw_wifi_t *test_wifi_join(const char *tag) {
#ifndef SYSTEM_NAME_PICO
  (void)tag;
  return NULL;
#else
  if (WIFI_SSID[0] == '\0') {
    sys_printf("[%s] no WIFI_SSID environment variable set, skipping\n", tag);
    return NULL;
  }

  hw_wifi_t *wifi = hw_wifi_init_client(NULL);
  if (wifi == NULL) {
    sys_printf("[%s] no Wi-Fi client backend on this board\n", tag);
    return NULL;
  }
  hw_wifi_set_callback(wifi, _test_wifi_on_event, NULL);

  sys_printf("[%s] scanning for \"%s\"...\n", tag, WIFI_SSID);
  test_assert(hw_wifi_scan(wifi));

  uint64_t start = sys_timestamp_ms();
  while (!_test_wifi_state.scan_done &&
        sys_timestamp_ms() - start < TEST_WIFI_TIMEOUT_MS) {
    hw_poll();
    sys_sleep_ms(TEST_WIFI_POLL_MS);
  }
  test_assert(_test_wifi_state.scan_done);
  test_assert(_test_wifi_state.found);

  sys_printf("[%s] found \"%s\", connecting...\n", tag, WIFI_SSID);
  _test_wifi_state.last_connect_event = 0;
  test_assert(
      hw_wifi_connect(wifi, &_test_wifi_state.found_network, WIFI_PASSWORD));
  test_assert(
      _test_wifi_wait(&_test_wifi_state.last_connect_event, TEST_WIFI_TIMEOUT_MS));
  test_assert(_test_wifi_state.last_connect_event == hw_wifi_event_connected);

  return wifi;
#endif
}

/** @brief Disconnect and release a handle from test_wifi_join(). No-op
 * on NULL (i.e. test_wifi_join() itself skipped or failed softly, or
 * this isn't a Pico build - it never returns anything else there). */
static void test_wifi_leave(const char *tag, hw_wifi_t *wifi) {
#ifndef SYSTEM_NAME_PICO
  (void)tag;
  (void)wifi;
#else
  if (wifi == NULL) {
    return;
  }
  sys_printf("[%s] disconnecting...\n", tag);
  _test_wifi_state.last_connect_event = 0;
  test_assert(hw_wifi_disconnect(wifi));
  test_assert(
      _test_wifi_wait(&_test_wifi_state.last_connect_event, TEST_WIFI_TIMEOUT_MS));
  test_assert(_test_wifi_state.last_connect_event == hw_wifi_event_disconnected);
  hw_wifi_deinit(wifi);
#endif
}
