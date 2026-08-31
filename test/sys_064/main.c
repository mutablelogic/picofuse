#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // Same grammar as sys_string_parse_int32 (shared implementation) -
  // one form each, just to confirm it's wired through correctly.

  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("123", 0, &v) == true);
    test_assert(v == 123);
  }
  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("-0x1A", 0, &v) == true);
    test_assert(v == -26);
  }
  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("0o17", 0, &v) == true);
    test_assert(v == 15);
  }
  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("0b1010", 0, &v) == true);
    test_assert(v == 10);
  }
  {
    test_assert(sys_string_parse_int64("12.5", 0, NULL) == false);
    test_assert(sys_string_parse_int64("abc", 0, NULL) == false);
    test_assert(sys_string_parse_int64(NULL, 0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // A value that overflows int32_t but fits comfortably in int64_t -
  // the whole reason a 64-bit variant exists.

  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("2147483648", 0, &v) == true); // INT32_MAX + 1
    test_assert(v == 2147483648LL);
  }
  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("-9999999999", 0, &v) == true);
    test_assert(v == -9999999999LL);
  }

  ///////////////////////////////////////////////////////////////////////
  // int64_t range boundaries

  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("9223372036854775807", 0, &v) == true);
    test_assert(v == 9223372036854775807LL);
  }
  {
    // Same value (INT64_MAX), in hex and binary - the exact boundary the
    // overflow check has to get right per-base, not just for decimal.
    int64_t v = 0;
    test_assert(sys_string_parse_int64("0x7FFFFFFFFFFFFFFF", 0, &v) == true);
    test_assert(v == 9223372036854775807LL);
  }
  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64(
                    "0b11111111111111111111111111111111"
                    "1111111111111111111111111111111",
                    0, &v) == true);
    test_assert(v == 9223372036854775807LL);
  }
  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("-9223372036854775808", 0, &v) == true);
    test_assert(v == (-9223372036854775807LL - 1)); // INT64_MIN
  }
  {
    // One past INT64_MAX.
    test_assert(sys_string_parse_int64("9223372036854775808", 0, NULL) == false);
  }
  {
    // One past INT64_MIN.
    test_assert(sys_string_parse_int64("-9223372036854775809", 0, NULL) == false);
  }
  {
    // Overflows even the uint64_t magnitude used internally.
    test_assert(
        sys_string_parse_int64("99999999999999999999999999", 0, NULL) == false);
  }
  {
    // The largest hex value that fits in a uint64_t magnitude, negated -
    // exercises the internal accumulator's own upper bound, not just
    // int64_t's.
    int64_t v = 0;
    test_assert(sys_string_parse_int64("0xFFFFFFFFFFFFFFFF", 0, &v) == false);
    (void)v; // out of int64_t range either way (magnitude == UINT64_MAX)
  }
  {
    // Same UINT64_MAX magnitude, spelled in binary (64 ones).
    test_assert(sys_string_parse_int64(
                    "0b1111111111111111111111111111111111111111111111111111"
                    "111111111111",
                    0, NULL) == false);
  }
  {
    // Too long in hex/binary to fit even the internal uint64_t
    // accumulator - not just too large for int64_t's own range.
    test_assert(sys_string_parse_int64("0xFFFFFFFFFFFFFFFFF", 0, NULL) == false);
    test_assert(sys_string_parse_int64(
                    "0b11111111111111111111111111111111111"
                    "11111111111111111111111111111111111",
                    0, NULL) == false);
  }

  // *value is left unchanged on a parse error.
  {
    int64_t v = -7;
    test_assert(sys_string_parse_int64("nope", 0, &v) == false);
    test_assert(v == -7);
  }

  ///////////////////////////////////////////////////////////////////////
  // len > 0 boundary enforcement, and NUL-termination not required.

  {
    int64_t v = 0;
    test_assert(sys_string_parse_int64("123abc", 3, &v) == true);
    test_assert(v == 123);
  }
  {
    // A shorter len bounds a shorter, still-complete number ("12").
    int64_t v = 0;
    test_assert(sys_string_parse_int64("123", 2, &v) == true);
    test_assert(v == 12);
  }
  {
    // Too long: digits end before len bytes are used.
    test_assert(sys_string_parse_int64("123", 4, NULL) == false);
  }
  {
    char buf[3] = {'4', '2', '9'}; // deliberately not NUL-terminated
    int64_t v = 0;
    test_assert(sys_string_parse_int64(buf, 3, &v) == true);
    test_assert(v == 429);
  }

}
