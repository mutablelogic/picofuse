#pragma once
#include <picofuse/net/types.h>
#include <stdbool.h>

/**
 * @brief Look up the address currently bound to a named network interface.
 * @param ifname BSD/Linux interface name, e.g. "en0" or "wlan0".
 * @param family Which address family to look up.
 * @param addr Set to the bound address on success, left untouched on
 * failure.
 * @return true if an address of the requested family was found and
 * written to @p addr, false otherwise (interface not found, no address of
 * that family bound, or getifaddrs() itself failed).
 *
 * Shared by any hw_wifi_t backend (Darwin's wifi.m today; a future Linux
 * wpa_supplicant-based one) that needs to answer hw_wifi_get_address() -
 * CoreWLAN/wpa_supplicant both manage association, not IP addressing, so
 * the actual DHCP-leased (or static/AP) address has to come from the OS's
 * own interface table instead, which getifaddrs() exposes identically on
 * both platforms. IPv6 addresses that are link-local (fe80::...) are
 * skipped in favor of a routable one, since a link-local address isn't
 * generally useful as "the" address of an interface.
 */
bool _hw_wifi_get_ifaddr(const char *ifname, net_addr_family_t family,
                         net_addr_t *addr);
