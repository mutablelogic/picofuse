#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_mqtt_init()/_deinit()/_default_config() handle lifecycle - pure
// state management, no real connection attempted (net_mqtt_connect() is
// still a stub), so this runs identically on every platform.

test_main_sys(0) {
  net_addr_t addr = net_addr_v4(127, 0, 0, 1);

  // NULL-safety.
  test_assert(net_mqtt_init(NULL, 0, 1000, NULL) == NULL); // addr required
  net_mqtt_deinit(NULL); // must not crash
  net_mqtt_default_config(NULL); // must not crash
  net_mqtt_set_callback(NULL, NULL, NULL); // must not crash

  // net_mqtt_default_config() fills client_id with a non-empty,
  // auto-generated id - see mqtt.h's own doc.
  net_mqtt_config_t cfg;
  net_mqtt_default_config(&cfg);
  test_assert(cfg.client_id != NULL);
  test_assert(cfg.client_id[0] != '\0');

  // Basic init/deinit round trip - port 0 exercises the NET_MQTT_PORT
  // default, config NULL exercises the same auto-generated client_id as
  // net_mqtt_default_config() above.
  net_mqtt_t *mqtt = net_mqtt_init(&addr, 0, 1000, NULL);
  test_assert(mqtt != NULL);

  // A singleton, not a pool - see net_mqtt_t's own doc on why. A second
  // net_mqtt_init() while the first is still active must fail.
  test_assert(net_mqtt_init(&addr, 0, 1000, NULL) == NULL);

  net_mqtt_deinit(mqtt);

  // The slot freed by net_mqtt_deinit() must be reusable, this time with
  // an explicit client_id.
  net_mqtt_config_t named = {.client_id = "picofuse-test"};
  net_mqtt_t *again = net_mqtt_init(&addr, 1883, 1000, &named);
  test_assert(again != NULL);
  net_mqtt_deinit(again);

  // net_mqtt_deinit() is idempotent - a second call on an already-deinited
  // handle must not crash or corrupt the singleton for the next init.
  net_mqtt_deinit(again);
  net_mqtt_t *reused = net_mqtt_init(&addr, 0, 1000, NULL);
  test_assert(reused != NULL);
  net_mqtt_deinit(reused);
}
