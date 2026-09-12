#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_open()'s timeout_ms actually bounds a connect that never gets any
// response at all - not the same thing net_004 already covers (a real,
// near-instant ECONNREFUSED over loopback). 192.0.2.1 is TEST-NET-1
// (RFC 5737): reserved for documentation, guaranteed never a real routed
// host, so any environment with a real net backend either black-holes
// the attempt (most common) or actively rejects it (some
// firewalls/NATs reject reserved ranges outright) - only the former
// actually exercises the timeout path, so the elapsed-time check below
// is soft, not asserted, the same way net_005's real-reply check is.

#define NET_012_TIMEOUT_MS 2000
// Generous slack for scheduling jitter, but tight enough to catch a
// regression back to NET_OPEN_DEFAULT_TIMEOUT_MS (30s) or an unbounded
// hang.
#define NET_012_MAX_ELAPSED_MS (NET_012_TIMEOUT_MS + 2000)

test_main_sys(0) {
  net_addr_t blackhole = net_addr_v4(192, 0, 2, 1); // TEST-NET-1

  uint64_t start = sys_timestamp_ms();
  sys_iostream_t *conn =
      net_open(net_proto_tcp, &blackhole, 80, NET_012_TIMEOUT_MS);
  uint64_t elapsed = sys_timestamp_ms() - start;

  test_assert(conn == NULL); // TEST-NET-1 is never a real reachable host

  if (elapsed < NET_012_TIMEOUT_MS) {
    sys_printf("[net_012] failed in %lldms, before timeout_ms=%d - no real "
              "net backend/route here (or this network actively rejects "
              "reserved ranges), not a genuine black hole - skipping the "
              "elapsed-time check\n",
              (long long)elapsed, NET_012_TIMEOUT_MS);
    return;
  }

  sys_printf("[net_012] blocked for %lldms (timeout_ms=%d)\n",
             (long long)elapsed, NET_012_TIMEOUT_MS);
  test_assert(elapsed < NET_012_MAX_ELAPSED_MS);
}
