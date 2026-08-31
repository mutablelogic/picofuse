#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// Advances *it and checks it matches one expected token, verifying both
// the metadata sys_scanner_next() reports and the actual text
// sys_scanner_token() retrieves for it.
static void expect_token(sys_scanner_t *it, const char *expect_text,
                          size_t expect_bytes, size_t expect_runes,
                          sys_scanner_class_t expect_isa) {
  bool ok = sys_scanner_next(it);
  test_assert(ok == true);
  test_assert(it->bytes == expect_bytes);
  test_assert(it->runes == expect_runes);
  test_assert(it->isa == expect_isa);

  char buf[64] = {0};
  test_assert(expect_bytes <= sizeof(buf));
  size_t got = sys_scanner_token(it, buf, sizeof(buf));
  test_assert(got == expect_bytes);
  test_assert(memcmp(buf, expect_text, expect_bytes) == 0);
}

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // "0x"/"0X" followed by hex digits is one number token, either case.

  {
    sys_iostream_t *s = sys_string_read("0x1A2f");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_hex);
    expect_token(&it, "0x1A2f", 6, 6, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("0XFF");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_hex);
    expect_token(&it, "0XFF", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // sys_scanner_numbers_hex includes sys_scanner_numbers.
  {
    sys_iostream_t *s = sys_string_read("42");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_hex);
    expect_token(&it, "42", 2, 2, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A signed hex number.
  {
    sys_iostream_t *s = sys_string_read("-0xAB");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_hex);
    expect_token(&it, "-0xAB", 5, 5, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // "0x" with no hex digit after it falls back to the number "0" plus
  // whatever "x..." tokenizes as on its own.

  {
    sys_iostream_t *s = sys_string_read("0x");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_hex);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    expect_token(&it, "x", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("0xZZ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_hex);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    expect_token(&it, "xZZ", 3, 3, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Never absorbed into a preceding punctuation run.

  {
    sys_iostream_t *s = sys_string_read("!0x1F");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_hex);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "0x1F", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Doesn't interfere with sys_scanner_keywords: a hex-looking sequence
  // still just continues an in-progress keyword run untouched.

  {
    sys_iostream_t *s = sys_string_read("abc0x1A");
    sys_scanner_t it = sys_scanner_init(
        s, sys_scanner_numbers_hex | sys_scanner_keywords);
    expect_token(&it, "abc0x1A", 7, 7, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

}
