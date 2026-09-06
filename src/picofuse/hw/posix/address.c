#include "private.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>

bool _hw_wifi_get_ifaddr(const char *ifname, net_addr_family_t family,
                         net_addr_t *addr) {
  if (ifname == NULL || addr == NULL) {
    return false;
  }

  struct ifaddrs *list;
  if (getifaddrs(&list) != 0) {
    return false;
  }

  bool found = false;
  for (struct ifaddrs *cur = list; cur != NULL; cur = cur->ifa_next) {
    if (cur->ifa_addr == NULL || strcmp(cur->ifa_name, ifname) != 0) {
      continue;
    }

    if (family == net_addr_family_v4 && cur->ifa_addr->sa_family == AF_INET) {
      const struct sockaddr_in *sin = (const struct sockaddr_in *)cur->ifa_addr;
      addr->family = net_addr_family_v4;
      memcpy(addr->addr.v4, &sin->sin_addr, sizeof(addr->addr.v4));
      found = true;
      break;
    }

    if (family == net_addr_family_v6 && cur->ifa_addr->sa_family == AF_INET6) {
      const struct sockaddr_in6 *sin6 =
          (const struct sockaddr_in6 *)cur->ifa_addr;
      if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
        continue; // keep looking for a routable address instead
      }
      addr->family = net_addr_family_v6;
      memcpy(addr->addr.v6, &sin6->sin6_addr, sizeof(addr->addr.v6));
      found = true;
      break;
    }
  }

  freeifaddrs(list);
  return found;
}
