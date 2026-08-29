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
  // Without the flag, a decimal point splits a number from what follows
  // it, same as any other punctuation.

  {
    sys_iostream_t *s = sys_string_read("3.14");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "3", 1, 1, sys_scanner_number);
    expect_token(&it, ".", 1, 1, sys_scanner_punct);
    expect_token(&it, "14", 2, 2, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A '.' immediately followed by digits is a fractional suffix on the
  // same number token.

  {
    sys_iostream_t *s = sys_string_read("3.14");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "3.14", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // sys_scanner_numbers_float includes sys_scanner_numbers.
  {
    sys_iostream_t *s = sys_string_read("42");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "42", 2, 2, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A signed float.
  {
    sys_iostream_t *s = sys_string_read("-0.5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "-0.5", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A '.' with no digit after it isn't a fraction - falls back to the
  // number, then the '.' as its own punctuation token.

  {
    sys_iostream_t *s = sys_string_read("3.");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "3", 1, 1, sys_scanner_number);
    expect_token(&it, ".", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("3.a");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "3", 1, 1, sys_scanner_number);
    expect_token(&it, ".", 1, 1, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A leading '.' never starts a number - "." isn't a sign or a digit.
  {
    sys_iostream_t *s = sys_string_read(".5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, ".", 1, 1, sys_scanner_punct);
    expect_token(&it, "5", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Only one fractional suffix - a second '.' isn't absorbed too.
  {
    sys_iostream_t *s = sys_string_read("1.2.3");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1.2", 3, 3, sys_scanner_number);
    expect_token(&it, ".", 1, 1, sys_scanner_punct);
    expect_token(&it, "3", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Without the flag, 'e'/'E' is never treated as an exponent marker -
  // it's just an ordinary letter, same as any other.

  {
    sys_iostream_t *s = sys_string_read("1e5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "1", 1, 1, sys_scanner_number);
    expect_token(&it, "e", 1, 1, sys_scanner_alpha);
    expect_token(&it, "5", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // 'e'/'E', an optional sign, then digits, is an exponent suffix on the
  // same number token - with or without a fractional part first.

  {
    sys_iostream_t *s = sys_string_read("1e5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1e5", 3, 3, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("1E5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1E5", 3, 3, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("1.5e-3");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1.5e-3", 6, 6, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("1.5e+3");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1.5e+3", 6, 6, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A signed mantissa with a signed exponent.
  {
    sys_iostream_t *s = sys_string_read("-1.5e-3");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "-1.5e-3", 7, 7, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Nothing valid after 'e' (or a sign with no digit after it) isn't an
  // exponent - falls back to the number, then whatever "e"/"e+"
  // tokenizes as on their own.

  {
    sys_iostream_t *s = sys_string_read("1e");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1", 1, 1, sys_scanner_number);
    expect_token(&it, "e", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("1ex");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1", 1, 1, sys_scanner_number);
    expect_token(&it, "ex", 2, 2, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("1e+");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "1", 1, 1, sys_scanner_number);
    expect_token(&it, "e", 1, 1, sys_scanner_alpha);
    expect_token(&it, "+", 1, 1, sys_scanner_symbol);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Never absorbed into a preceding punctuation run.

  {
    sys_iostream_t *s = sys_string_read("!3.14");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "3.14", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("!1e5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers_float);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "1e5", 3, 3, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
