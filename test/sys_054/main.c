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
  // Without the flag, digits and a leading sign are ordinary separate
  // tokens, exactly as before.

  {
    sys_iostream_t *s = sys_string_read("-123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "-", 1, 1, sys_scanner_punct);
    expect_token(&it, "123", 3, 3, sys_scanner_digit);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Unsigned whole numbers.

  {
    sys_iostream_t *s = sys_string_read("0");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "0", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "123", 3, 3, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Leading '-' or '+' immediately followed by a digit is part of the
  // number.

  {
    sys_iostream_t *s = sys_string_read("-123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "-123", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("+123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "+123", 4, 4, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A lone sign not followed by a digit falls back to ordinary
  // punct/symbol classification instead of being consumed as a number.

  {
    sys_iostream_t *s = sys_string_read("-a");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "-", 1, 1, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("+a");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "+", 1, 1, sys_scanner_symbol);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A sign at the very end of the stream.
  {
    sys_iostream_t *s = sys_string_read("-");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "-", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A sign that fails to start a number still merges normally into a
  // surrounding punctuation run - the failed lookahead doesn't leave
  // anything stuck or skipped.

  {
    sys_iostream_t *s = sys_string_read("!-a");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "!-", 2, 2, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A number is never absorbed into a preceding punctuation/symbol run,
  // even though the sign character shares that run's class.

  {
    sys_iostream_t *s = sys_string_read("!-5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "-5", 2, 2, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("!5");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "5", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A run-internal '-' followed by digits still starts its own number,
  // splitting what would otherwise be one merged digit run.
  {
    sys_iostream_t *s = sys_string_read("12-34");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "12", 2, 2, sys_scanner_number);
    expect_token(&it, "-34", 3, 3, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Composes with sys_scanner_nospaces.

  {
    sys_iostream_t *s = sys_string_read(" -5  6 ");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_numbers | sys_scanner_nospaces);
    expect_token(&it, "-5", 2, 2, sys_scanner_number);
    expect_token(&it, "6", 1, 1, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Composes with sys_scanner_keywords: a digit run right after a letter
  // run still merges into the keyword as it always did - the number
  // guard doesn't interfere with keyword continuation.

  {
    sys_iostream_t *s = sys_string_read("abc123 -45");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_numbers | sys_scanner_keywords);
    expect_token(&it, "abc123", 6, 6, sys_scanner_keyword);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "-45", 3, 3, sys_scanner_number);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A leading digit run still isn't a keyword even with both flags on -
  // it's a number, same rule as before ("a leading digit run is never a
  // keyword").
  {
    sys_iostream_t *s = sys_string_read("123abc");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_numbers | sys_scanner_keywords);
    expect_token(&it, "123", 3, 3, sys_scanner_number);
    expect_token(&it, "abc", 3, 3, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
