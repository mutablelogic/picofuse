#include "../private.h"
#include <picofuse/net.h>
#include <picofuse/sys.h>

// Generous enough for "<sys_env_name()>-<sys_env_serial()>" on every
// target this project builds for, without needing to be exact - a
// caller-supplied client_id longer than this is simply truncated (see
// sys_sprintf()'s own truncation semantics).
#define NET_MQTT_CLIENT_ID_SIZE 64

///////////////////////////////////////////////////////////////////////////////
// TYPES

// A singleton, not a pool - see net_mqtt_t's own doc on why.
struct net_mqtt_t {
  net_addr_t addr;
  uint16_t port;
  uint32_t timeout_ms;
  char client_id[NET_MQTT_CLIENT_ID_SIZE];
  net_mqtt_event_callback_t callback;
  void *userdata;
  bool active;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct net_mqtt_t _net_mqtt_singleton = {0};
static char _net_mqtt_default_client_id[NET_MQTT_CLIENT_ID_SIZE];

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

// Stable per-device id derived from the environment's own name and serial
// number - shared by net_mqtt_default_config() and net_mqtt_init()'s own
// fallback, so both use the same derivation.
static const char *_net_mqtt_generate_client_id(void) {
  sys_sprintf(_net_mqtt_default_client_id, sizeof(_net_mqtt_default_client_id),
              "%s-%s", sys_env_name(), sys_env_serial());
  return _net_mqtt_default_client_id;
}

void net_mqtt_default_config(net_mqtt_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->client_id = _net_mqtt_generate_client_id();
}

net_mqtt_t *net_mqtt_init(const net_addr_t *addr, uint16_t port,
                          uint32_t timeout_ms,
                          const net_mqtt_config_t *config) {
  // Unlike net_ntp_init(), there's no single "standard" public broker to
  // fall back to here - time.cloudflare.com is a legitimate default NTP
  // source for anyone, but the equivalent for MQTT would be a public test
  // broker (test.mosquitto.org, broker.hivemq.com, ...), which isn't
  // something a real device should silently default to. addr is required.
  if (_net_mqtt_singleton.active || addr == NULL) {
    return NULL;
  }

  net_mqtt_config_t default_config;
  if (config == NULL) {
    net_mqtt_default_config(&default_config);
    config = &default_config;
  }

  _net_mqtt_singleton.addr = *addr;
  _net_mqtt_singleton.port = (port != 0) ? port : NET_MQTT_PORT;
  _net_mqtt_singleton.timeout_ms = timeout_ms;
  _net_mqtt_singleton.callback = NULL;
  _net_mqtt_singleton.userdata = NULL;

  const char *client_id = (config->client_id != NULL && config->client_id[0] != '\0')
                              ? config->client_id
                              : _net_mqtt_generate_client_id();
  sys_sprintf(_net_mqtt_singleton.client_id,
             sizeof(_net_mqtt_singleton.client_id), "%s", client_id);

  _net_mqtt_singleton.active = true;
  return &_net_mqtt_singleton;
}

void net_mqtt_set_callback(net_mqtt_t *mqtt, net_mqtt_event_callback_t callback,
                           void *userdata) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton ||
      !_net_mqtt_singleton.active) {
    return;
  }
  _net_mqtt_singleton.callback = callback;
  _net_mqtt_singleton.userdata = userdata;
}

void net_mqtt_deinit(net_mqtt_t *mqtt) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton ||
      !_net_mqtt_singleton.active) {
    return;
  }
  // TODO: disconnect first once net_mqtt_connect()/net_mqtt_disconnect()
  // actually open a connection - see net_mqtt_deinit()'s own doc.
  _net_mqtt_singleton.callback = NULL;
  _net_mqtt_singleton.userdata = NULL;
  _net_mqtt_singleton.active = false;
}

///////////////////////////////////////////////////////////////////////////////
// CONNECTION

/** Stub implementation: MQTT connect/publish/subscribe not yet
 * implemented - only the handle lifecycle above is real so far. */
bool net_mqtt_connect(net_mqtt_t *mqtt) {
  (void)mqtt;
  return false;
}

/** Stub implementation - see net_mqtt_connect()'s own note above. */
void net_mqtt_disconnect(net_mqtt_t *mqtt) { (void)mqtt; }

///////////////////////////////////////////////////////////////////////////////
// PUBLISH

/** Stub implementation - see net_mqtt_connect()'s own note above. */
bool net_mqtt_publish(net_mqtt_t *mqtt, const char *topic, const void *payload,
                      size_t payload_len, net_mqtt_qos_t qos, bool retain) {
  (void)mqtt;
  (void)topic;
  (void)payload;
  (void)payload_len;
  (void)qos;
  (void)retain;
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// SUBSCRIBE

/** Stub implementation - see net_mqtt_connect()'s own note above. */
bool net_mqtt_subscribe(net_mqtt_t *mqtt, const char *topic,
                        net_mqtt_qos_t qos) {
  (void)mqtt;
  (void)topic;
  (void)qos;
  return false;
}

/** Stub implementation - see net_mqtt_connect()'s own note above. */
bool net_mqtt_unsubscribe(net_mqtt_t *mqtt, const char *topic) {
  (void)mqtt;
  (void)topic;
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// POLLING

/** Stub implementation - nothing can ever be connected yet, since
 * net_mqtt_connect() always fails - see its own note above. */
bool _net_mqtt_poll(void) { return false; }
