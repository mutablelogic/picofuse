#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // Same shared grammar as sys_string_parse_int64 - one form each.

  {
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64("123", 0, &v) == true);
    test_assert(v == 123);
  }
  {
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64("0x1A", 0, &v) == true);
    test_assert(v == 26);
  }
  {
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64("0o17", 0, &v) == true);
    test_assert(v == 15);
  }

  ///////////////////////////////////////////////////////////////////////
  // No negative representation - a leading '-' is always a parse error,
  // even for "-0".

  {
    uint64_t v = 42;
    test_assert(sys_string_parse_uint64("-5", 0, &v) == false);
    test_assert(v == 42); // unchanged
  }
  {
    test_assert(sys_string_parse_uint64("-0", 0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // A value that overflows uint32_t but fits comfortably in uint64_t.

  {
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64("4294967296", 0, &v) == true); // UINT32_MAX + 1
    test_assert(v == 4294967296ULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // uint64_t range boundaries

  {
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64("18446744073709551615", 0, &v) == true);
    test_assert(v == 18446744073709551615ULL); // UINT64_MAX
  }
  {
    // One past UINT64_MAX - overflows even the internal uint64_t
    // magnitude accumulator.
    test_assert(sys_string_parse_uint64("18446744073709551616", 0, NULL) == false);
  }
  {
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64("0xFFFFFFFFFFFFFFFF", 0, &v) == true);
    test_assert(v == 18446744073709551615ULL);
  }
  {
    // UINT64_MAX spelled in binary (64 ones).
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64(
                    "0b1111111111111111111111111111111111111111111111111111"
                    "111111111111",
                    0, &v) == true);
    test_assert(v == 18446744073709551615ULL);
  }
  {
    // Too long in hex/binary to fit even the internal uint64_t
    // accumulator - not just too large for uint64_t's own range.
    test_assert(sys_string_parse_uint64("0xFFFFFFFFFFFFFFFFF", 0, NULL) == false);
    test_assert(sys_string_parse_uint64(
                    "0b11111111111111111111111111111111111"
                    "11111111111111111111111111111111111",
                    0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed - same rejection rules as the signed variant

  {
    test_assert(sys_string_parse_uint64("12.5", 0, NULL) == false);
    test_assert(sys_string_parse_uint64("abc", 0, NULL) == false);
    test_assert(sys_string_parse_uint64(NULL, 0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // len > 0, str need not be NUL-terminated.

  {
    char buf[3] = {'4', '2', '9'}; // deliberately not NUL-terminated
    uint64_t v = 0;
    test_assert(sys_string_parse_uint64(buf, 3, &v) == true);
    test_assert(v == 429);
  }

}
