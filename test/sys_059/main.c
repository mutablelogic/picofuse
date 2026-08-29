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
  // Punctuation is always one rune per token, even when the same
  // character repeats or different punctuation characters are adjacent -
  // unlike sys_scanner_alpha/digit/space, it's never a run.

  {
    sys_iostream_t *s = sys_string_read("!!!");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // The motivating case: adjacent structural delimiters of a
  // bracket-and-comma grammar like JSON must never merge, or nested
  // structures like "[[1,2],[3,4]]" become unparseable ("],[" would
  // otherwise be one 3-byte punct token instead of three).
  {
    sys_iostream_t *s = sys_string_read("[[1,2],[3,4]]");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_numbers);
    expect_token(&it, "[", 1, 1, sys_scanner_punct);
    expect_token(&it, "[", 1, 1, sys_scanner_punct);
    expect_token(&it, "1", 1, 1, sys_scanner_number);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "2", 1, 1, sys_scanner_number);
    expect_token(&it, "]", 1, 1, sys_scanner_punct);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "[", 1, 1, sys_scanner_punct);
    expect_token(&it, "3", 1, 1, sys_scanner_number);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "4", 1, 1, sys_scanner_number);
    expect_token(&it, "]", 1, 1, sys_scanner_punct);
    expect_token(&it, "]", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Symbol characters get the same treatment.

  {
    sys_iostream_t *s = sys_string_read("+++");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "+", 1, 1, sys_scanner_symbol);
    expect_token(&it, "+", 1, 1, sys_scanner_symbol);
    expect_token(&it, "+", 1, 1, sys_scanner_symbol);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("<=>");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "<", 1, 1, sys_scanner_symbol);
    expect_token(&it, "=", 1, 1, sys_scanner_symbol);
    expect_token(&it, ">", 1, 1, sys_scanner_symbol);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Control characters too. Note this is genuinely non-whitespace
  // control codes only - '\t' and friends are sys_scanner_space, not
  // sys_scanner_control, and merge as a run like any other whitespace
  // (see sys_rune_isa()'s doc comment).

  {
    sys_iostream_t *s = sys_string_read("\x01\x01\x01");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "\x01", 1, 1, sys_scanner_control);
    expect_token(&it, "\x01", 1, 1, sys_scanner_control);
    expect_token(&it, "\x01", 1, 1, sys_scanner_control);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("\t\t\t");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "\t\t\t", 3, 3, sys_scanner_space);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Contrast: sys_scanner_space is still a run - this is specific to
  // punct/symbol/newline/control, not a blanket "nothing merges" change.

  {
    sys_iostream_t *s = sys_string_read("   ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "   ", 3, 3, sys_scanner_space);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // And alpha/digit runs are unaffected too.
  {
    sys_iostream_t *s = sys_string_read("abc123");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "abc", 3, 3, sys_scanner_alpha);
    expect_token(&it, "123", 3, 3, sys_scanner_digit);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
