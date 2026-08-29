#include <picofuse/sys.h>
#include <test/test.h>

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // Plain decimal, signed and unsigned

  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("123", 0, &v) == true);
    test_assert(v == 123);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("-123", 0, &v) == true);
    test_assert(v == -123);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("+123", 0, &v) == true);
    test_assert(v == 123);
  }
  {
    int32_t v = 1;
    test_assert(sys_string_parse_int32("0", 0, &v) == true);
    test_assert(v == 0);
  }
  {
    int32_t v = 1;
    test_assert(sys_string_parse_int32("-0", 0, &v) == true);
    test_assert(v == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Hex - either case, either prefix case

  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0x1A", 0, &v) == true);
    test_assert(v == 26);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0X1a", 0, &v) == true);
    test_assert(v == 26);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("-0xFF", 0, &v) == true);
    test_assert(v == -255);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0x0", 0, &v) == true);
    test_assert(v == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Octal - both the "0o" prefixed and bare leading-zero forms

  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0o17", 0, &v) == true);
    test_assert(v == 15);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0O17", 0, &v) == true);
    test_assert(v == 15);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0755", 0, &v) == true);
    test_assert(v == 493);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("-0755", 0, &v) == true);
    test_assert(v == -493);
  }
  {
    // '8'/'9' aren't octal digits - stays plain decimal, same as the
    // scanner's sys_scanner_numbers_octal behavior.
    int32_t v = 0;
    test_assert(sys_string_parse_int32("089", 0, &v) == true);
    test_assert(v == 89);
  }

  ///////////////////////////////////////////////////////////////////////
  // Binary

  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0b1010", 0, &v) == true);
    test_assert(v == 10);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0B1010", 0, &v) == true);
    test_assert(v == 10);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("-0b1", 0, &v) == true);
    test_assert(v == -1);
  }

  ///////////////////////////////////////////////////////////////////////
  // int32_t range boundaries

  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("2147483647", 0, &v) == true);
    test_assert(v == 2147483647);
  }
  {
    // Same value (INT32_MAX), in hex and binary - the exact boundary the
    // overflow check has to get right per-base, not just for decimal.
    int32_t v = 0;
    test_assert(sys_string_parse_int32("0x7FFFFFFF", 0, &v) == true);
    test_assert(v == 2147483647);
  }
  {
    int32_t v = 0;
    test_assert(
        sys_string_parse_int32("0b1111111111111111111111111111111", 0, &v) ==
        true);
    test_assert(v == 2147483647);
  }
  {
    int32_t v = 0;
    test_assert(sys_string_parse_int32("-2147483648", 0, &v) == true);
    test_assert(v == (-2147483647 - 1)); // INT32_MIN, without an overflowing literal
  }
  {
    // One past INT32_MAX.
    test_assert(sys_string_parse_int32("2147483648", 0, NULL) == false);
  }
  {
    // One past INT32_MIN.
    test_assert(sys_string_parse_int32("-2147483649", 0, NULL) == false);
  }
  {
    // Wildly out of range, well beyond even a 64-bit magnitude's reach
    // in this test's intent - just needs to be rejected, not crash.
    test_assert(sys_string_parse_int32("99999999999999999999", 0, NULL) == false);
  }

  // Too long in hex/binary - the overflow check has to work the same
  // way regardless of which base produced the magnitude, not just for
  // decimal literals.
  {
    // 0x100000000 == 2^32, one past UINT32_MAX and well past INT32_MAX.
    test_assert(sys_string_parse_int32("0x100000000", 0, NULL) == false);
    test_assert(sys_string_parse_int32("-0x100000000", 0, NULL) == false);
  }
  {
    // Same value (2^32), spelled in binary.
    test_assert(sys_string_parse_int32("0b100000000000000000000000000000000", 0,
                                       NULL) == false);
    test_assert(sys_string_parse_int32("-0b100000000000000000000000000000000",
                                       0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed - all parse errors, especially floats

  {
    test_assert(sys_string_parse_int32(NULL, 0, NULL) == false);
    test_assert(sys_string_parse_int32("", 0, NULL) == false);
    test_assert(sys_string_parse_int32("+", 0, NULL) == false);
    test_assert(sys_string_parse_int32("-", 0, NULL) == false);
    test_assert(sys_string_parse_int32("abc", 0, NULL) == false);
    test_assert(sys_string_parse_int32("0x", 0, NULL) == false);
    test_assert(sys_string_parse_int32("0xZZ", 0, NULL) == false);
    test_assert(sys_string_parse_int32("0b", 0, NULL) == false);
    test_assert(sys_string_parse_int32("0b2", 0, NULL) == false);
    test_assert(sys_string_parse_int32("123 ", 0, NULL) == false); // trailing space
    test_assert(sys_string_parse_int32(" 123", 0, NULL) == false); // leading space
    test_assert(sys_string_parse_int32("123abc", 0, NULL) == false);
  }

  // Floats specifically - the whole point of this function's strictness.
  {
    test_assert(sys_string_parse_int32("12.5", 0, NULL) == false);
    test_assert(sys_string_parse_int32("1e10", 0, NULL) == false);
    test_assert(sys_string_parse_int32(".5", 0, NULL) == false);
    test_assert(sys_string_parse_int32("5.", 0, NULL) == false);
  }

  // *value is left unchanged on a parse error.
  {
    int32_t v = 42;
    test_assert(sys_string_parse_int32("not a number", 0, &v) == false);
    test_assert(v == 42);
  }

  ///////////////////////////////////////////////////////////////////////
  // len == 0: str's own NUL terminator stands in for len - trailing
  // content is a parse error, not truncated/ignored.

  {
    test_assert(sys_string_parse_int32("123abc", 0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // len > 0: the number must consume exactly len bytes.

  {
    // Bounding just the numeric prefix of a longer buffer succeeds.
    int32_t v = 0;
    test_assert(sys_string_parse_int32("123abc", 3, &v) == true);
    test_assert(v == 123);
  }
  {
    // A shorter len isn't "too short" by itself - it just bounds a
    // shorter, still-complete number ("12", not a truncated "123").
    int32_t v = 0;
    test_assert(sys_string_parse_int32("123", 2, &v) == true);
    test_assert(v == 12);
  }
  {
    // Too long: digits end before len bytes are used.
    test_assert(sys_string_parse_int32("123", 4, NULL) == false);
  }
  {
    // A genuine "too short" case: len lands right after a prefix, with
    // no digit consumed yet.
    test_assert(sys_string_parse_int32("0x1A", 2, NULL) == false);
  }
  {
    // str need not be NUL-terminated at all when len is given.
    char buf[3] = {'4', '2', '9'}; // deliberately not NUL-terminated
    int32_t v = 0;
    test_assert(sys_string_parse_int32(buf, 3, &v) == true);
    test_assert(v == 429);
  }

  sys_exit();
  return 0;
}
