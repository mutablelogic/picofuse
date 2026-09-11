#include "private.h"

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

struct net_mqtt_t _net_mqtt_singleton = {0};
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
  config->username = NULL;
  config->password = NULL;
}

net_mqtt_t *net_mqtt_init(const net_addr_t *addr, uint16_t port,
                          uint32_t timeout_ms,
                          const net_mqtt_config_t *config) {
  if (_net_mqtt_singleton.active || addr == NULL) {
    return NULL;
  }

  // Create sync primitives
  if (_net_mqtt_singleton.lock == NULL) {
    _net_mqtt_singleton.lock = sys_mutex_init();
    if (_net_mqtt_singleton.lock == NULL) {
      return NULL;
    }
  }
  if (_net_mqtt_singleton.publish_cond == NULL) {
    _net_mqtt_singleton.publish_cond = sys_cond_init();
    if (_net_mqtt_singleton.publish_cond == NULL) {
      return NULL; // lock stays allocated - reused on the next init()
    }
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
  _net_mqtt_singleton.connected = false;
  _net_mqtt_singleton.conn = NULL;
  _net_mqtt_singleton.next_message_id = 0;
  _net_mqtt_singleton.next_packet_id = 0;
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_idle;
  _net_mqtt_singleton.subscribe.state = _net_mqtt_subscribe_idle;
  _net_mqtt_singleton.subscribe.topic = NULL;
  _net_mqtt_singleton.unsubscribe.state = _net_mqtt_unsubscribe_idle;
  _net_mqtt_singleton.unsubscribe.topic = NULL;
  for (size_t i = 0; i < NET_MQTT_TOPIC_CAPACITY; i++) {
    _net_mqtt_singleton.topics[i].active = false;
  }

  const char *client_id =
      (config->client_id != NULL && config->client_id[0] != '\0')
          ? config->client_id
          : _net_mqtt_generate_client_id();
  sys_sprintf(_net_mqtt_singleton.client_id,
              sizeof(_net_mqtt_singleton.client_id), "%s", client_id);

  sys_sprintf(_net_mqtt_singleton.username,
              sizeof(_net_mqtt_singleton.username), "%s",
              (config->username != NULL) ? config->username : "");
  sys_sprintf(_net_mqtt_singleton.password,
              sizeof(_net_mqtt_singleton.password), "%s",
              (config->password != NULL) ? config->password : "");

  _net_mqtt_singleton.active = true;
  return &_net_mqtt_singleton;
}

void net_mqtt_set_callback(net_mqtt_t *mqtt, net_mqtt_event_callback_t callback,
                           void *userdata) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || !mqtt->active) {
    return;
  }
  // Same lock _net_mqtt_fire_event() reads these two fields under - keeps
  // a concurrent set_callback() from tearing a dispatch that's mid-read
  // (matches pix_display_set_callback()'s identical reasoning).
  sys_mutex_lock(mqtt->lock);
  mqtt->callback = callback;
  mqtt->userdata = userdata;
  sys_mutex_unlock(mqtt->lock);
}

void net_mqtt_deinit(net_mqtt_t *mqtt) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || !mqtt->active) {
    return;
  }
  net_mqtt_disconnect(mqtt); // no-op if not connected - see its own doc
  mqtt->callback = NULL;
  mqtt->userdata = NULL;
  mqtt->active = false;
  // lock/publish_cond deliberately outlive this - see their own doc.
}
