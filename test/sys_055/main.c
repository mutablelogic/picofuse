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

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // Without sys_scanner_numbers_octal, a leading zero followed by
  // octal-looking digits is just an ordinary decimal number.

  {
    sys_iostream_t *s = sys_string_read("0755");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "0755", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Bare form (C style): a leading zero directly followed by octal
  // digits, no marker, is one number token.

  {
    sys_iostream_t *s = sys_string_read("0755");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "0755", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // sys_scanner_numbers_octal includes sys_scanner_numbers - no need to
  // OR the two together.
  {
    sys_iostream_t *s = sys_string_read("42");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "42", 2, 2, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A signed octal number.
  {
    sys_iostream_t *s = sys_string_read("-017");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "-017", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // '8'/'9' aren't octal digits - the run just stops there, falling
  // back to an ordinary decimal number, not an error.

  {
    sys_iostream_t *s = sys_string_read("089");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "089", 3, 3, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // An octal digit run followed by a non-octal decimal digit splits into
  // two number tokens - the octal one, then a fresh decimal one.
  {
    sys_iostream_t *s = sys_string_read("0779");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "077", 3, 3, sys_scanner_number);
    expect_token(&it, "9", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A bare "0" with nothing octal-looking after it.
  {
    sys_iostream_t *s = sys_string_read("0");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Prefixed form (Python/Rust/Swift style): "0o"/"0O" followed by octal
  // digits is one number token, checked before the bare form.

  {
    sys_iostream_t *s = sys_string_read("0o17");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "0o17", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("0O17");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "0O17", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A signed prefixed octal number.
  {
    sys_iostream_t *s = sys_string_read("-0o17");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "-0o17", 5, 5, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // "0o" (or a bare "0" immediately followed by '8'/'9' that also isn't
  // a valid octal digit for the bare form) with no octal digit after the
  // prefix falls back to the ordinary number "0" plus whatever follows
  // as its own token - the prefix only commits once a digit confirms it,
  // same rule as sys_scanner_numbers_hex/_binary.
  {
    sys_iostream_t *s = sys_string_read("0o");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    expect_token(&it, "o", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("0o9");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    expect_token(&it, "o", 1, 1, sys_scanner_alpha);
    expect_token(&it, "9", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Never absorbed into a preceding punctuation run - either form.

  {
    sys_iostream_t *s = sys_string_read("!0755");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "0755", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("!0o17");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_octal);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "0o17", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
