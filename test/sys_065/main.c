#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // Same shared grammar as sys_string_parse_int32 - one form each, just
  // to confirm it's wired through correctly.

  {
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("123", 0, &v) == true);
    test_assert(v == 123);
  }
  {
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("+123", 0, &v) == true);
    test_assert(v == 123);
  }
  {
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("0x1A", 0, &v) == true);
    test_assert(v == 26);
  }
  {
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("0755", 0, &v) == true);
    test_assert(v == 493);
  }
  {
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("0b1010", 0, &v) == true);
    test_assert(v == 10);
  }
  {
    uint32_t v = 1;
    test_assert(sys_string_parse_uint32("0", 0, &v) == true);
    test_assert(v == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // No negative representation - a leading '-' is always a parse error,
  // even for "-0".

  {
    uint32_t v = 42;
    test_assert(sys_string_parse_uint32("-5", 0, &v) == false);
    test_assert(v == 42); // unchanged
  }
  {
    test_assert(sys_string_parse_uint32("-0", 0, NULL) == false);
  }
  {
    test_assert(sys_string_parse_uint32("-0x1A", 0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // uint32_t range boundaries

  {
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("4294967295", 0, &v) == true);
    test_assert(v == 4294967295u);
  }
  {
    // Same value (UINT32_MAX), in hex and binary - the exact boundary
    // the overflow check has to get right per-base, not just decimal.
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("0xFFFFFFFF", 0, &v) == true);
    test_assert(v == 4294967295u);
  }
  {
    uint32_t v = 0;
    test_assert(
        sys_string_parse_uint32("0b11111111111111111111111111111111", 0, &v) ==
        true);
    test_assert(v == 4294967295u);
  }
  {
    // One past UINT32_MAX.
    test_assert(sys_string_parse_uint32("4294967296", 0, NULL) == false);
  }

  // Too long in hex/binary - the overflow check has to work the same
  // way regardless of which base produced the magnitude, not just for
  // decimal literals.
  {
    // 0x100000000 == 2^32, one past UINT32_MAX.
    test_assert(sys_string_parse_uint32("0x100000000", 0, NULL) == false);
  }
  {
    // Same value (2^32), spelled in binary.
    test_assert(sys_string_parse_uint32("0b100000000000000000000000000000000", 0,
                                        NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed - same rejection rules as the signed variant

  {
    test_assert(sys_string_parse_uint32(NULL, 0, NULL) == false);
    test_assert(sys_string_parse_uint32("", 0, NULL) == false);
    test_assert(sys_string_parse_uint32("abc", 0, NULL) == false);
    test_assert(sys_string_parse_uint32("12.5", 0, NULL) == false);
    test_assert(sys_string_parse_uint32("1e10", 0, NULL) == false);
    test_assert(sys_string_parse_uint32("123abc", 0, NULL) == false);
    test_assert(sys_string_parse_uint32("0x", 0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // len > 0: same exact-boundary rule as the signed variant.

  {
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32("123abc", 3, &v) == true);
    test_assert(v == 123);
  }
  {
    char buf[3] = {'4', '2', '9'}; // deliberately not NUL-terminated
    uint32_t v = 0;
    test_assert(sys_string_parse_uint32(buf, 3, &v) == true);
    test_assert(v == 429);
  }

}
