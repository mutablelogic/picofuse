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
  // Without the flag, letters and digits still split into separate
  // tokens exactly as before.

  {
    sys_iostream_t *s = sys_string_read("abc123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "abc", 3, 3, sys_scanner_alpha);
    expect_token(&it, "123", 3, 3, sys_scanner_digit);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // With sys_scanner_keywords: letters and digits merge into one token,
  // as long as it starts with a letter.

  {
    sys_iostream_t *s = sys_string_read("abc123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_keywords);
    expect_token(&it, "abc123", 6, 6, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A single letter is a complete keyword on its own.
  {
    sys_iostream_t *s = sys_string_read("x");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_keywords);
    expect_token(&it, "x", 1, 1, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A leading digit run is never a keyword, but a letter-starting run
  // right after it still is - this is a per-token rule, not "seen a
  // digit, never keyword again".
  {
    sys_iostream_t *s = sys_string_read("123abc");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_keywords);
    expect_token(&it, "123", 3, 3, sys_scanner_digit);
    expect_token(&it, "abc", 3, 3, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Multiple identifiers separated by non-identifier characters.
  {
    sys_iostream_t *s = sys_string_read("foo bar1 2baz");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_keywords);
    expect_token(&it, "foo", 3, 3, sys_scanner_keyword);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "bar1", 4, 4, sys_scanner_keyword);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "2", 1, 1, sys_scanner_digit);
    expect_token(&it, "baz", 3, 3, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_scanner_keyword_withunderscores includes sys_scanner_keywords -
  // no need to OR the two together.

  {
    sys_iostream_t *s = sys_string_read("my_var_123");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_keyword_withunderscores);
    expect_token(&it, "my_var_123", 10, 10, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Without that flag, an underscore still breaks the run (falls back
  // to ordinary punctuation classification for '_').
  {
    sys_iostream_t *s = sys_string_read("my_var");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_keywords);
    expect_token(&it, "my", 2, 2, sys_scanner_keyword);
    expect_token(&it, "_", 1, 1, sys_scanner_punct);
    expect_token(&it, "var", 3, 3, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // An underscore can't start a keyword, even with the flag - only a
  // letter can.
  {
    sys_iostream_t *s = sys_string_read("_foo");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_keyword_withunderscores);
    expect_token(&it, "_", 1, 1, sys_scanner_punct);
    expect_token(&it, "foo", 3, 3, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_scanner_keyword_withdashes includes sys_scanner_keywords too.

  {
    sys_iostream_t *s = sys_string_read("my-var-123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_keyword_withdashes);
    expect_token(&it, "my-var-123", 10, 10, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Without that flag, a dash breaks the run (ordinary punctuation).
  {
    sys_iostream_t *s = sys_string_read("my-var");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_keywords);
    expect_token(&it, "my", 2, 2, sys_scanner_keyword);
    expect_token(&it, "-", 1, 1, sys_scanner_punct);
    expect_token(&it, "var", 3, 3, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Combining both modifiers (OR them together) allows both '_' and '-'.

  {
    sys_iostream_t *s = sys_string_read("my_var-name");
    sys_scanner_t it = sys_scanner_init(
        s, sys_scanner_keyword_withunderscores | sys_scanner_keyword_withdashes);
    expect_token(&it, "my_var-name", 11, 11, sys_scanner_keyword);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
