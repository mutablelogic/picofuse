#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// METHODS

net_addr_t net_addr_v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  net_addr_t addr;
  addr.family = net_addr_family_v4;
  addr.addr.v4[0] = a;
  addr.addr.v4[1] = b;
  addr.addr.v4[2] = c;
  addr.addr.v4[3] = d;
  return addr;
}

net_addr_t net_addr_v4_any(void) { return net_addr_v4(0, 0, 0, 0); }

net_addr_t net_addr_v6(const uint8_t bytes[16]) {
  net_addr_t addr;
  addr.family = net_addr_family_v6;
  memcpy(addr.addr.v6, bytes, sizeof(addr.addr.v6));
  return addr;
}

net_addr_t net_addr_v6_any(void) {
  net_addr_t addr;
  addr.family = net_addr_family_v6;
  memset(addr.addr.v6, 0, sizeof(addr.addr.v6));
  return addr;
}

size_t net_addr_to_string(const net_addr_t *addr, char *buf,
                          size_t buf_size) {
  if (addr == NULL) {
    if (buf != NULL && buf_size > 0) {
      buf[0] = '\0';
    }
    return 0;
  }

  if (addr->family == net_addr_family_v4) {
    return sys_sprintf(buf, buf_size, "%u.%u.%u.%u",
                       (unsigned)addr->addr.v4[0], (unsigned)addr->addr.v4[1],
                       (unsigned)addr->addr.v4[2], (unsigned)addr->addr.v4[3]);
  }

  // IPv6 - full form (8 groups of 4 hex digits), no "::" zero-compression.
  return sys_sprintf(
      buf, buf_size, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
      (unsigned)addr->addr.v6[0], (unsigned)addr->addr.v6[1],
      (unsigned)addr->addr.v6[2], (unsigned)addr->addr.v6[3],
      (unsigned)addr->addr.v6[4], (unsigned)addr->addr.v6[5],
      (unsigned)addr->addr.v6[6], (unsigned)addr->addr.v6[7],
      (unsigned)addr->addr.v6[8], (unsigned)addr->addr.v6[9],
      (unsigned)addr->addr.v6[10], (unsigned)addr->addr.v6[11],
      (unsigned)addr->addr.v6[12], (unsigned)addr->addr.v6[13],
      (unsigned)addr->addr.v6[14], (unsigned)addr->addr.v6[15]);
}
