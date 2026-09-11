#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_mqtt_publish() genuinely blocks (not busy-waits) when a previous
// publish is still staged, and wakes up once either net_poll() drains it
// (from another thread/core) or the connection drops - see its own doc.
// Against a real broker (test.mosquitto.org) since publish() requires a
// connected handle - skips (not asserted) if no reply arrives at all,
// same reasoning as net_013's own comment.

#define NET_016_TIMEOUT_MS 5000
// Time given to the worker thread to actually reach its blocking wait
// before the main thread acts - without this, "the worker hasn't
// finished yet" wouldn't actually prove the wait itself works, just
// that publish() eventually returns.
#define NET_016_SETTLE_MS 200
#define NET_016_STILL_BLOCKED 0xFFFFFFFFu

static net_mqtt_t *g_mqtt;
static sys_atomic_t g_worker_result;
static sys_waitgroup_t *g_wg;

static void worker_publish(void *arg) {
  (void)arg;
  uint32_t id = net_mqtt_publish(g_mqtt, "picofuse/test/net_016", NULL, 0,
                                 net_mqtt_qos_0, false);
  sys_atomic_set(&g_worker_result, id);
  sys_waitgroup_done(g_wg);
}

static void spawn_worker(void) {
  g_wg = sys_waitgroup_init();
  test_assert(g_wg != NULL);
  test_assert(sys_waitgroup_add(g_wg, 1));
  sys_atomic_set(&g_worker_result, NET_016_STILL_BLOCKED);
#if defined(SYSTEM_NAME_PICO)
  // Only one spare core (core1) on Pico.
  test_assert(sys_thread_create_on_core(worker_publish, NULL, 1));
#else
  test_assert(sys_thread_create(worker_publish, NULL));
#endif
}

test_main_sys(0) {
  net_addr_t addr = net_addr_v4(54, 36, 178, 49); // test.mosquitto.org
  g_mqtt = net_mqtt_init(&addr, NET_MQTT_PORT, NET_016_TIMEOUT_MS, NULL);
  test_assert(g_mqtt != NULL);

  if (!net_mqtt_connect(g_mqtt)) {
    sys_printf("[net_016] no CONNACK - no network route, skipping\n");
    net_mqtt_deinit(g_mqtt);
    return;
  }

  // --- Case 1: a blocked publish() wakes up once net_poll() (from this,
  // the "other" thread/core relative to the worker) drains the slot it
  // was waiting on. ---
  uint32_t id1 = net_mqtt_publish(g_mqtt, "picofuse/test/net_016", NULL, 0,
                                  net_mqtt_qos_0, false);
  test_assert(id1 != 0);

  spawn_worker();
  sys_sleep_ms(NET_016_SETTLE_MS);
  test_assert(sys_atomic_get(&g_worker_result) == NET_016_STILL_BLOCKED);

  test_assert(net_poll()); // Sends id1, frees the slot, broadcasts.

  sys_waitgroup_wait(g_wg);
  sys_waitgroup_deinit(g_wg);
  uint32_t id2 = sys_atomic_get(&g_worker_result);
  test_assert(id2 != 0 && id2 != NET_016_STILL_BLOCKED);
  test_assert(id2 > id1);
  sys_printf("[net_016] worker unblocked once poll() freed the slot, id=%u\n",
            (unsigned)id2);

  test_assert(net_poll()); // Drain the worker's own staged publish too.

  // --- Case 2: a blocked publish() wakes up (and fails, 0) once the
  // connection drops instead of a slot ever freeing up. ---
  uint32_t id3 = net_mqtt_publish(g_mqtt, "picofuse/test/net_016", NULL, 0,
                                  net_mqtt_qos_0, false);
  test_assert(id3 != 0);

  spawn_worker();
  sys_sleep_ms(NET_016_SETTLE_MS);
  test_assert(sys_atomic_get(&g_worker_result) == NET_016_STILL_BLOCKED);

  net_mqtt_disconnect(g_mqtt); // Should wake the blocked worker with 0.

  sys_waitgroup_wait(g_wg);
  sys_waitgroup_deinit(g_wg);
  test_assert(sys_atomic_get(&g_worker_result) == 0);
  sys_printf("[net_016] worker correctly woke to 0 on disconnect\n");

  net_mqtt_deinit(g_mqtt);
}
