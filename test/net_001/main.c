#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_addr_v4()/_v4_any()/_v6()/_v6_any()/_to_string() - pure formatting/
// construction logic, no sockets, so this runs identically on every
// platform (including the Pico stub backend, which doesn't affect these).

test_main_sys(0) {
  char buf[64];

  net_addr_t v4 = net_addr_v4(192, 168, 1, 42);
  test_assert(v4.family == net_addr_family_v4);
  test_assert(v4.addr.v4[0] == 192 && v4.addr.v4[1] == 168 &&
              v4.addr.v4[2] == 1 && v4.addr.v4[3] == 42);
  test_assert(net_addr_to_string(&v4, buf, sizeof(buf)) == 12);
  test_assert_strequal(buf, "192.168.1.42");

  net_addr_t v4_any = net_addr_v4_any();
  test_assert(v4_any.family == net_addr_family_v4);
  net_addr_to_string(&v4_any, buf, sizeof(buf));
  test_assert_strequal(buf, "0.0.0.0");

  const uint8_t v6_bytes[16] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  net_addr_t v6 = net_addr_v6(v6_bytes);
  test_assert(v6.family == net_addr_family_v6);
  for (int i = 0; i < 16; i++) {
    test_assert(v6.addr.v6[i] == v6_bytes[i]);
  }
  net_addr_to_string(&v6, buf, sizeof(buf));
  test_assert_strequal(buf, "2001:0db8:0000:0000:0000:0000:0000:0001");

  net_addr_t v6_any = net_addr_v6_any();
  test_assert(v6_any.family == net_addr_family_v6);
  for (int i = 0; i < 16; i++) {
    test_assert(v6_any.addr.v6[i] == 0);
  }
  net_addr_to_string(&v6_any, buf, sizeof(buf));
  test_assert_strequal(buf, "0000:0000:0000:0000:0000:0000:0000:0000");

  // Truncation semantics match sys_sprintf(): the return value is the
  // length that would have been written, not what actually fit.
  char tiny[4];
  size_t want = net_addr_to_string(&v4, tiny, sizeof(tiny));
  test_assert(want == 12);
  test_assert(strlen(tiny) == sizeof(tiny) - 1);

  // NULL-safety.
  test_assert(net_addr_to_string(NULL, buf, sizeof(buf)) == 0);
}
