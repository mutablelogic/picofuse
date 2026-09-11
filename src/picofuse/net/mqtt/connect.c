#include "private.h"
#include <string.h>

// MQTT 3.1.1, not 5.0 - simpler, fixed-shape packets (no recurring
// Property-Length-prefixed TLV block every packet type would otherwise
// need), and still the more universally-supported wire format. Nothing
// this client does yet (no Will, no per-message properties) needs what
// 5.0 adds over it.

// Large enough for a CONNECT packet with the longest possible client_id,
// username, and password all at once (fixed header 1-4 + variable
// header 10 + three length-prefixed fields, each up to 2 +
// NET_MQTT_CLIENT_ID_SIZE/NET_MQTT_CREDENTIAL_SIZE) with room to spare.
#define _NET_MQTT_CONNECT_BUF_SIZE 384

bool net_mqtt_connect(net_mqtt_t *mqtt) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton || !_net_mqtt_singleton.active) {
    return false;
  }

  // Held for the whole handshake below, net_open() included - this is
  // the one operation that actually establishes the connection, so a
  // concurrent connect()/disconnect()/publish() call genuinely has to
  // wait for it to finish (or fail) rather than racing it. See this
  // module's lock discipline: released before any event fires, so a
  // callback that calls back into this module doesn't deadlock on it.
  sys_mutex_lock(_net_mqtt_singleton.lock);

  if (_net_mqtt_singleton.connected) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  sys_iostream_t *conn =
      net_open(net_proto_tcp, &_net_mqtt_singleton.addr,
               _net_mqtt_singleton.port, _net_mqtt_singleton.timeout_ms);
  if (conn == NULL) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  // Build the CONNECT packet
  size_t client_id_len = strlen(_net_mqtt_singleton.client_id);

  // Password only travels alongside a non-empty username - MQTT 3.1.1
  // requires that pairing (see net_mqtt_config_t::password's own doc), so
  // a password with no username is silently dropped here rather than
  // sent in a shape the spec disallows.
  size_t username_len = strlen(_net_mqtt_singleton.username);
  size_t password_len = (username_len > 0) ? strlen(_net_mqtt_singleton.password) : 0;

  uint8_t connect_flags = _NET_MQTT_CONNECT_FLAG_CLEAN_SESSION;
  uint32_t remaining_length = 10 + 2 + (uint32_t)client_id_len;
  if (username_len > 0) {
    connect_flags |= _NET_MQTT_CONNECT_FLAG_USERNAME;
    remaining_length += 2 + (uint32_t)username_len;
  }
  if (password_len > 0) {
    connect_flags |= _NET_MQTT_CONNECT_FLAG_PASSWORD;
    remaining_length += 2 + (uint32_t)password_len;
  }

  uint8_t buf[_NET_MQTT_CONNECT_BUF_SIZE];
  size_t pos = 0;
  buf[pos++] = _NET_MQTT_PACKET_CONNECT;

  size_t len_n =
      _net_mqtt_encode_length(remaining_length, buf + pos, sizeof(buf) - pos);
  if (len_n == 0) {
    sys_iostream_close(conn);
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }
  pos += len_n;

  buf[pos++] = 0x00; // Protocol Name Length (MSB)
  buf[pos++] = 0x04; // Protocol Name Length (LSB) - "MQTT" is 4 bytes
  buf[pos++] = 'M';
  buf[pos++] = 'Q';
  buf[pos++] = 'T';
  buf[pos++] = 'T';
  buf[pos++] = _NET_MQTT_PROTOCOL_LEVEL;
  buf[pos++] = connect_flags;
  buf[pos++] = (uint8_t)(_NET_MQTT_KEEPALIVE_S >> 8);
  buf[pos++] = (uint8_t)(_NET_MQTT_KEEPALIVE_S & 0xFF);

  // Payload: Client Identifier, then [User Name], then [Password] - this
  // exact order, per the spec (no Will fields, since this client doesn't
  // support one).
  buf[pos++] = (uint8_t)(client_id_len >> 8);
  buf[pos++] = (uint8_t)(client_id_len & 0xFF);
  memcpy(buf + pos, _net_mqtt_singleton.client_id, client_id_len);
  pos += client_id_len;

  if (username_len > 0) {
    buf[pos++] = (uint8_t)(username_len >> 8);
    buf[pos++] = (uint8_t)(username_len & 0xFF);
    memcpy(buf + pos, _net_mqtt_singleton.username, username_len);
    pos += username_len;
  }
  if (password_len > 0) {
    buf[pos++] = (uint8_t)(password_len >> 8);
    buf[pos++] = (uint8_t)(password_len & 0xFF);
    memcpy(buf + pos, _net_mqtt_singleton.password, password_len);
    pos += password_len;
  }

  if (!_net_mqtt_write_exact(conn, buf, pos, _net_mqtt_singleton.timeout_ms)) {
    sys_iostream_close(conn);
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  // CONNACK is always exactly 4 bytes in 3.1.1
  uint8_t connack[4];
  if (!_net_mqtt_read_exact(conn, connack, sizeof(connack),
                            _net_mqtt_singleton.timeout_ms)) {
    sys_iostream_close(conn);
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }
  if (connack[0] != _NET_MQTT_PACKET_CONNACK || connack[1] != 0x02 ||
      connack[3] != 0x00 /* Connect Return Code: 0 = accepted */) {
    sys_iostream_close(conn);
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return false;
  }

  _net_mqtt_singleton.conn = conn;
  _net_mqtt_singleton.connected = true;
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  net_mqtt_event_t event = {.type = net_mqtt_event_connected};
  _net_mqtt_fire_event(&event);
  return true;
}

void net_mqtt_disconnect(net_mqtt_t *mqtt) {
  if (mqtt == NULL || mqtt != &_net_mqtt_singleton) {
    return;
  }

  sys_mutex_lock(_net_mqtt_singleton.lock);
  if (!_net_mqtt_singleton.connected) {
    sys_mutex_unlock(_net_mqtt_singleton.lock);
    return;
  }

  // Best-effort: MQTT 3.1.1's DISCONNECT is a one-way notification with
  // no broker reply to wait for (unlike 5.0's two-way version) - a
  // failed/partial write here still ends at the same place as a
  // successful one, the teardown below.
  uint8_t packet[2] = {_NET_MQTT_PACKET_DISCONNECT, 0x00};
  _net_mqtt_write_exact(_net_mqtt_singleton.conn, packet, sizeof(packet),
                        _net_mqtt_singleton.timeout_ms);

  _net_mqtt_abort_connection_locked();
  sys_mutex_unlock(_net_mqtt_singleton.lock);

  net_mqtt_event_t event = {.type = net_mqtt_event_disconnected};
  _net_mqtt_fire_event(&event);
}
