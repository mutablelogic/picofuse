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
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Whitespace runs are their own sys_scanner_space token, same shape as
  // the rune tokenizer - the scanner never filters tokens out on its
  // own; a caller that wants spaces skipped does that itself by
  // checking it.isa after sys_scanner_next().

  {
    sys_iostream_t *s = sys_string_read("hello world");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "hello", 5, 5, sys_scanner_alpha);
    test_assert(it.start == 0);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "world", 5, 5, sys_scanner_alpha);
    test_assert(it.start == 6);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Leading, trailing, and multi-space runs are each one token.
  {
    sys_iostream_t *s = sys_string_read("  hello   world  ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "  ", 2, 2, sys_scanner_space);
    expect_token(&it, "hello", 5, 5, sys_scanner_alpha);
    expect_token(&it, "   ", 3, 3, sys_scanner_space);
    expect_token(&it, "world", 5, 5, sys_scanner_alpha);
    expect_token(&it, "  ", 2, 2, sys_scanner_space);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // All-whitespace input is one space token.
  {
    sys_iostream_t *s = sys_string_read("   ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "   ", 3, 3, sys_scanner_space);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Every base class, including malformed bytes as sys_scanner_other

  {
    sys_iostream_t *s = sys_string_read("1$\x01\x80");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
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
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "  ", 2, 2, sys_scanner_space);
    expect_token(&it, "Hello", 5, 5, sys_scanner_alpha);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "   ", 3, 3, sys_scanner_space);
    expect_token(&it, "World", 5, 5, sys_scanner_alpha);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "123", 3, 3, sys_scanner_digit);
    expect_token(&it, "  ", 2, 2, sys_scanner_space);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
