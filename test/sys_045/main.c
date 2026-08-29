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
  // NULL / empty string, either way

  {
    sys_scanner_t it = sys_scanner_init(NULL, sys_scanner_none);
    test_assert(sys_scanner_next(&it) == false);
    test_assert(sys_scanner_next(&it) == false); // repeatable, no crash
  }
  {
    sys_iostream_t *s = sys_string_read("");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Default flags: space tokens are emitted, same shape as the rune
  // tokenizer

  {
    sys_iostream_t *s = sys_string_read("hello world");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "hello", 5, 5, sys_scanner_alpha);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "world", 5, 5, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // nospace: whitespace runs are skipped entirely, not returned

  {
    sys_iostream_t *s = sys_string_read("hello world");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    expect_token(&it, "hello", 5, 5, sys_scanner_alpha);
    test_assert(it.start == 0);
    expect_token(&it, "world", 5, 5, sys_scanner_alpha);
    test_assert(it.start == 6); // the space at offset 5 was skipped, not returned
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Leading and trailing whitespace vanish entirely - no empty-ish
  // boundary tokens.
  {
    sys_iostream_t *s = sys_string_read("  hello  ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    expect_token(&it, "hello", 5, 5, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A multi-space run is skipped as a single unit.
  {
    sys_iostream_t *s = sys_string_read("hello   world");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    expect_token(&it, "hello", 5, 5, sys_scanner_alpha);
    expect_token(&it, "world", 5, 5, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // All-whitespace input yields no tokens at all.
  {
    sys_iostream_t *s = sys_string_read("   ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Non-space runs on either side of a skipped space stay separate
  // tokens - nospace only affects the space class.
  {
    sys_iostream_t *s = sys_string_read("a, b");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Every base class, and malformed bytes classify as sys_scanner_other
  // and are never skipped (nospace only ever affects sys_scanner_space)

  {
    sys_iostream_t *s = sys_string_read("1$\x01\x80");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    expect_token(&it, "1", 1, 1, sys_scanner_digit);
    expect_token(&it, "$", 1, 1, sys_scanner_symbol);
    expect_token(&it, "\x01", 1, 1, sys_scanner_control);
    expect_token(&it, "\x80", 1, 1, sys_scanner_other);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A fuller mixed walk

  {
    sys_iostream_t *s = sys_string_read("  Hello,   World! 123  ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_nospaces);
    expect_token(&it, "Hello", 5, 5, sys_scanner_alpha);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "World", 5, 5, sys_scanner_alpha);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "123", 3, 3, sys_scanner_digit);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
