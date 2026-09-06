#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_ntp_init()/_read()/_deinit() against a real NTP server
// (time.cloudflare.com) over the network. net_ntp_init() always succeeds
// regardless of platform/backend - it just records addr/port/timeout_ms,
// it doesn't open anything until net_ntp_read() does (see its own doc) -
// so what can legitimately be unavailable is a real reply: skipped (not
// asserted) if none arrives, since that needs actual internet access,
// which an offline/sandboxed environment (or Pico's still-stubbed net
// backend - see net/pico/socket.c's own @todo) won't have.

#define NET_005_TIMEOUT_MS 3000

test_main_sys(0) {
  // NULL-safety.
  test_assert(net_ntp_read(NULL, NULL) == false);
  net_ntp_deinit(NULL); // must not crash

  net_addr_t explicit_addr = net_addr_v4(162, 159, 200, 1); // cloudflare
  net_ntp_t *ntp =
      net_ntp_init(&explicit_addr, NET_NTP_PORT, NET_005_TIMEOUT_MS);
  test_assert(ntp != NULL);

  // The singleton is exclusive - a second init while this one is active
  // must fail, even with different (here, defaulted) arguments.
  test_assert(net_ntp_init(NULL, 0, NET_005_TIMEOUT_MS) == NULL);

  sys_date_t date = {0};
  if (!net_ntp_read(ntp, &date)) {
    sys_printf("[net_005] no NTP reply - no network route, skipping\n");
    net_ntp_deinit(ntp);
    return;
  }
  sys_printf("[net_005] date: seconds=%lld\n", (long long)date.seconds);
  test_assert(date.seconds > 1704067200); // after 2024-01-01
  test_assert(date.tzoffset == 0);

  // Repeatable on the same handle - each call is an independent round
  // trip (see net_ntp_read()'s own doc: nothing is held open between
  // calls), so time should never appear to run backwards between them.
  sys_date_t date2 = {0};
  test_assert(net_ntp_read(ntp, &date2));
  test_assert(date2.seconds >= date.seconds);

  net_ntp_deinit(ntp);

  // The singleton is free again once deinited - and NULL/0 default to
  // time.cloudflare.com/NET_NTP_PORT.
  net_ntp_t *ntp2 = net_ntp_init(NULL, 0, NET_005_TIMEOUT_MS);
  test_assert(ntp2 != NULL);
  sys_date_t date3 = {0};
  if (net_ntp_read(ntp2, &date3)) {
    test_assert(date3.seconds > 1704067200);
  }
  net_ntp_deinit(ntp2);
}
