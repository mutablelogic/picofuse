#include "private.h"

// Same polling granularity ntp.c's own read loop uses.
#define _NET_MQTT_POLL_MS 20

void _net_mqtt_fire_event(const net_mqtt_event_t *event) {
  // Snapshot under the lock (same one connect.c/publish.c hold for their
  // own I/O - see net_mqtt_set_callback()'s matching write), then call
  // the callback itself outside it - a callback that calls back into
  // net_mqtt_connect()/_publish()/etc. must not deadlock on a lock this
  // function is still holding.
  sys_mutex_lock(_net_mqtt_singleton.lock);
  net_mqtt_event_callback_t callback = _net_mqtt_singleton.callback;
  void *userdata = _net_mqtt_singleton.userdata;
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  if (callback != NULL) {
    callback(&_net_mqtt_singleton, event, userdata);
  }
}

void _net_mqtt_abort_connection_locked(void) {
  if (!_net_mqtt_singleton.connected) {
    return;
  }
  sys_iostream_close(_net_mqtt_singleton.conn);
  _net_mqtt_singleton.conn = NULL;
  _net_mqtt_singleton.connected = false;

  // Whatever net_mqtt_publish() staged (if anything) can never be sent
  // on a connection that no longer exists - see net_mqtt_publish()'s own
  // doc on this being a silent abandonment, no event of its own.
  _net_mqtt_singleton.publish.state = _net_mqtt_publish_idle;

  // Wakes a net_mqtt_publish() call blocked waiting for either a free
  // slot or a disconnect - this is the disconnect it's also watching for
  // (see net_mqtt_publish()'s own doc).
  sys_cond_broadcast(_net_mqtt_singleton.publish_cond);
}

bool _net_mqtt_write_exact(sys_iostream_t *conn, const void *data, size_t n,
                           uint32_t timeout_ms) {
  if (conn == NULL) {
    return false;
  }
  const char *bytes = data;
  size_t wrote = 0;
  uint64_t start = sys_timestamp_ms();
  while (wrote < n) {
    wrote += sys_iostream_write(conn, bytes + wrote, n - wrote);
    if (wrote < n) {
      if (sys_timestamp_ms() - start >= timeout_ms) {
        return false;
      }
      sys_sleep_ms(_NET_MQTT_POLL_MS);
    }
  }
  return true;
}

bool _net_mqtt_read_exact(sys_iostream_t *conn, void *data, size_t n,
                          uint32_t timeout_ms) {
  if (conn == NULL) {
    return false;
  }
  char *bytes = data;
  size_t got = 0;
  uint64_t start = sys_timestamp_ms();
  while (got < n) {
    got += sys_iostream_read(conn, bytes + got, n - got);
    if (got < n) {
      if (sys_timestamp_ms() - start >= timeout_ms) {
        return false;
      }
      sys_sleep_ms(_NET_MQTT_POLL_MS);
    }
  }
  return true;
}

size_t _net_mqtt_encode_length(uint32_t value, uint8_t *buf, size_t buf_size) {
  if (value > 0x0FFFFFFFu) { // 268,435,455 - MQTT's own 4-byte ceiling
    return 0;
  }
  size_t n = 0;
  do {
    if (n >= buf_size) {
      return 0;
    }
    uint8_t byte = (uint8_t)(value % 128);
    value /= 128;
    if (value > 0) {
      byte |= 0x80;
    }
    buf[n++] = byte;
  } while (value > 0);
  return n;
}
