#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>

// Standard 48-byte NTP client/server packet - see net_ntp_read()'s own
// doc for which fields this actually uses.
#define NET_NTP_PACKET_SIZE 48

// Seconds between the NTP epoch (1900-01-01) and the Unix epoch
// (1970-01-01) - subtracted from a reply's transmit timestamp to get a
// sys_date_t's own seconds-since-Unix-epoch field.
#define NET_NTP_EPOCH_OFFSET 2208988800ULL

// How often net_ntp_read()'s own wait-for-reply loop re-checks, between
// sys_iostream_read() attempts - matches the polling granularity used
// throughout this project's own net_00N tests.
#define NET_NTP_POLL_MS 20

///////////////////////////////////////////////////////////////////////////////
// TYPES

// A singleton, not a pool, matching hw_wifi_t's own reasoning (see
// hw/wifi.h's doc on why there's no pool there): a device only ever needs
// one NTP source at a time. Doesn't hold a socket open between reads -
// see net_ntp_read()'s own doc on why.
struct net_ntp_t {
  net_addr_t addr;
  uint16_t port;
  uint32_t timeout_ms;
  bool active;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct net_ntp_t _net_ntp_singleton = {0};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

net_ntp_t *net_ntp_init(const net_addr_t *addr, uint16_t port,
                        uint32_t timeout_ms) {
  if (_net_ntp_singleton.active) {
    return NULL;
  }

  _net_ntp_singleton.addr =
      (addr != NULL) ? *addr : net_addr_v4(NET_NTP_DEFAULT_ADDR);
  _net_ntp_singleton.port = (port != 0) ? port : NET_NTP_PORT;
  _net_ntp_singleton.timeout_ms = timeout_ms;
  _net_ntp_singleton.active = true;
  return &_net_ntp_singleton;
}

void net_ntp_deinit(net_ntp_t *ntp) {
  if (ntp == NULL || ntp != &_net_ntp_singleton || !_net_ntp_singleton.active) {
    return;
  }
  _net_ntp_singleton.active = false;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool net_ntp_read(net_ntp_t *ntp, sys_date_t *date) {
  if (ntp == NULL || ntp != &_net_ntp_singleton || !_net_ntp_singleton.active ||
      date == NULL) {
    return false;
  }

  // Open UDP connection to the NTP server.
  sys_iostream_t *conn =
      net_open(net_proto_udp, &_net_ntp_singleton.addr,
              _net_ntp_singleton.port, _net_ntp_singleton.timeout_ms);
  if (conn == NULL) {
    sys_debugf("net", "ntp: net_open failed");
    return false;
  }

  // A client request is the same 48-byte shape as a reply, with every
  // field but the first byte left zeroed: LI=0 (no warning), VN=4
  // (NTPv4), Mode=3 (client) - 0b00'100'011 = 0x23.
  uint8_t packet[NET_NTP_PACKET_SIZE] = {0};
  packet[0] = 0x23;

  size_t wrote = sys_iostream_write(conn, (char *)packet, sizeof(packet));
  if (wrote != sizeof(packet)) {
    sys_debugf("net", "ntp: write failed, wrote %zu/%zu bytes", wrote,
               sizeof(packet));
    sys_iostream_close(conn);
    return false;
  }

  size_t got = 0;
  uint64_t start = sys_timestamp_ms();
  while (got < sizeof(packet) &&
         sys_timestamp_ms() - start < _net_ntp_singleton.timeout_ms) {
    got += sys_iostream_read(conn, (char *)packet + got, sizeof(packet) - got);
    if (got < sizeof(packet)) {
      sys_sleep_ms(NET_NTP_POLL_MS);
    }
  }
  sys_iostream_close(conn);
  if (got != sizeof(packet)) {
    sys_debugf("net", "ntp: read timed out after %ums, got %zu/%zu bytes",
               (unsigned)_net_ntp_singleton.timeout_ms, got, sizeof(packet));
    return false;
  }

  // Transmit timestamp (bytes 40-47): the moment the server sent this
  // reply, as a 32-bit whole-seconds count (since the NTP epoch) followed
  // by a 32-bit binary fraction of a second - both big-endian. This is
  // the one field a plain SNTP client needs; see ntp.h's own doc on why
  // the rest of the exchange (origin/receive timestamps, root delay/
  // dispersion, stratum, ...) goes unused here.
  uint32_t ntp_seconds = ((uint32_t)packet[40] << 24) |
                         ((uint32_t)packet[41] << 16) |
                         ((uint32_t)packet[42] << 8) | (uint32_t)packet[43];
  uint32_t ntp_fraction = ((uint32_t)packet[44] << 24) |
                          ((uint32_t)packet[45] << 16) |
                          ((uint32_t)packet[46] << 8) | (uint32_t)packet[47];

  if (ntp_seconds < NET_NTP_EPOCH_OFFSET) {
    sys_debugf("net", "ntp: implausible reply, ntp_seconds=%u", ntp_seconds);
    return false; // a pre-1970 reply is nonsensical - reject rather than
                  // wrap negative
  }

  date->seconds = (int64_t)(ntp_seconds - NET_NTP_EPOCH_OFFSET);
  date->nanoseconds = (int32_t)(((uint64_t)ntp_fraction * 1000000000ULL) >> 32);
  date->tzoffset = 0;
  return true;
}
