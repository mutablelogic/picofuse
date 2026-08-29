#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// Advances *it and checks it matches one expected token, verifying both
// the metadata sys_rune_tokenize_next() reports and the actual text
// sys_rune_tokenize_token() retrieves for it.
static void expect_token(sys_rune_tokenize_t *it, const char *expect_text,
                          size_t expect_bytes, size_t expect_runes,
                          sys_rune_class_t expect_isa) {
  bool ok = sys_rune_tokenize_next(it);
  test_assert(ok == true);
  test_assert(it->bytes == expect_bytes);
  test_assert(it->runes == expect_runes);
  test_assert(it->isa == expect_isa);

  char buf[64] = {0};
  test_assert(expect_bytes <= sizeof(buf));
  size_t got = sys_rune_tokenize_token(it, buf, sizeof(buf));
  test_assert(got == expect_bytes);
  test_assert(memcmp(buf, expect_text, expect_bytes) == 0);
}

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // NULL / empty string: no tokens

  {
    sys_rune_tokenize_t it = sys_rune_tokenize_init(NULL);
    test_assert(sys_rune_tokenize_next(&it) == false);
    // Calling again must not crash and must keep returning false.
    test_assert(sys_rune_tokenize_next(&it) == false);
  }
  {
    sys_iostream_t *s = sys_string_read("");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Single token

  {
    sys_iostream_t *s = sys_string_read("hello");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    expect_token(&it, "hello", 5, 5, sys_rune_alpha);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Word / space / word

  {
    sys_iostream_t *s = sys_string_read("hello world");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    expect_token(&it, "hello", 5, 5, sys_rune_alpha);
    expect_token(&it, " ", 1, 1, sys_rune_space);
    expect_token(&it, "world", 5, 5, sys_rune_alpha);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Letters / digits split into separate tokens

  {
    sys_iostream_t *s = sys_string_read("abc123");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    expect_token(&it, "abc", 3, 3, sys_rune_alpha);
    expect_token(&it, "123", 3, 3, sys_rune_digit);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Multi-byte UTF-8 within a single token: byte count != rune count

  {
    const char *str = "caf\xC3\xA9"; // "café", e-acute precomposed (2 bytes)
    sys_iostream_t *s = sys_string_read(str);
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    expect_token(&it, str, 5, 4, sys_rune_alpha);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed UTF-8: stray continuation bytes group together as "other",
  // and interrupt a run of letters they're inserted into.

  {
    sys_iostream_t *s = sys_string_read("\x80\x80");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    expect_token(&it, "\x80\x80", 2, 2, sys_rune_other);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }
  {
    sys_iostream_t *s = sys_string_read("ab\x80"
                                         "cd");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    expect_token(&it, "ab", 2, 2, sys_rune_alpha);
    expect_token(&it, "\x80", 1, 1, sys_rune_other);
    expect_token(&it, "cd", 2, 2, sys_rune_alpha);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Punctuation run

  {
    sys_iostream_t *s = sys_string_read("!!!");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    expect_token(&it, "!!!", 3, 3, sys_rune_punct);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Full walk: tokens are contiguous and cover the whole string exactly
  // (no gaps, no overlaps, no truncation), bounded so a tokenizer bug
  // that never terminates fails cleanly instead of hanging.

  {
    const char *str = "Hello, World! 123 caf\xC3\xA9 \x80 done.";
    size_t total_len = strlen(str);
    sys_iostream_t *s = sys_string_read(str);
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    size_t covered = 0;
    int guard = 0;
    while (sys_rune_tokenize_next(&it)) {
      test_assert(it.start == (ptrdiff_t)covered); // exactly where the last one ended
      test_assert(it.bytes > 0);
      covered += it.bytes;
      test_assert(++guard < 100);
    }
    test_assert(covered == total_len);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_tokenize_token: truncation when cap is smaller than the
  // token, and that it restores the stream position for tokenizing to
  // continue correctly afterward.

  {
    sys_iostream_t *s = sys_string_read("hello world");
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    test_assert(sys_rune_tokenize_next(&it) == true); // "hello"

    char buf[3] = {0};
    size_t got = sys_rune_tokenize_token(&it, buf, sizeof(buf));
    test_assert(got == 3);
    test_assert(memcmp(buf, "hel", 3) == 0);

    // cap == 0 is a safe no-op truncation, not a crash.
    test_assert(sys_rune_tokenize_token(&it, buf, 0) == 0);

    // Tokenizing continues correctly after reading (and truncating) a
    // token's text.
    expect_token(&it, " ", 1, 1, sys_rune_space);
    expect_token(&it, "world", 5, 5, sys_rune_alpha);
    test_assert(sys_rune_tokenize_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
