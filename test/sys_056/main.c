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
  // Without the flag, "0b01" is just the number "0" followed by a
  // keyword-less alpha/digit split ("b" then "01").

  {
    sys_iostream_t *s = sys_string_read("0b01");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    expect_token(&it, "01", 2, 2, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // "0b"/"0B" followed by binary digits is one number token.

  {
    sys_iostream_t *s = sys_string_read("0b01010101");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_binary);
    expect_token(&it, "0b01010101", 10, 10, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("0B11");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_binary);
    expect_token(&it, "0B11", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // sys_scanner_numbers_binary includes sys_scanner_numbers.
  {
    sys_iostream_t *s = sys_string_read("42");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_binary);
    expect_token(&it, "42", 2, 2, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A signed binary number.
  {
    sys_iostream_t *s = sys_string_read("-0b101");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_binary);
    expect_token(&it, "-0b101", 6, 6, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // "0b" with no binary digit after it falls back to the number "0"
  // plus whatever "b..." tokenizes as on its own - the prefix only
  // commits once a digit confirms it.

  {
    sys_iostream_t *s = sys_string_read("0b");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_binary);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("0b2");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_binary);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    expect_token(&it, "2", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Never absorbed into a preceding punctuation run.

  {
    sys_iostream_t *s = sys_string_read("!0b11");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_binary);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "0b11", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

}
