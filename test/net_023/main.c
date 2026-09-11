#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// net_mqtt_publish() rejects a topic too long to encode in the 2-byte
// Topic Name Length field (max 65535 bytes) rather than silently
// truncating that length prefix while still writing every real topic
// byte, which would desync the connection's framing for whatever's sent
// after it. Pure input validation - no real connection needed, since the
// check happens before net_mqtt_publish() even looks at connection
// state.

#define NET_023_OVERSIZED_TOPIC_LEN (0xFFFF + 1)

test_main_sys(0) {
  net_addr_t addr = net_addr_v4(127, 0, 0, 1);
  net_mqtt_t *mqtt = net_mqtt_init(&addr, NET_MQTT_PORT, 1000, NULL);
  test_assert(mqtt != NULL);

  char *topic = sys_malloc(NET_023_OVERSIZED_TOPIC_LEN + 1);
  test_assert(topic != NULL);
  memset(topic, 'a', NET_023_OVERSIZED_TOPIC_LEN);
  topic[NET_023_OVERSIZED_TOPIC_LEN] = '\0';

  test_assert(net_mqtt_publish(mqtt, topic, NULL, 0, net_mqtt_qos_0, false) ==
             0);

  sys_free(topic);
  net_mqtt_deinit(mqtt);
  sys_printf("[net_023] oversized topic rejected\n");
}
