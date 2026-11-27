#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// A stream of bytes which may include zero bytes
static sys_iostream_t *open_bytes(char *buf, size_t cap, const char *bytes,
                                  size_t length) {
  sys_iostream_t *s = sys_string_open(buf, cap);
  test_assert(s != NULL);
  test_assert(sys_iostream_write(s, bytes, length) == length);
  test_assert(sys_iostream_seek(s, 0, true) == 0);
  return s;
}

static void expect_token(sys_scanner_t *it, const char *text, size_t bytes,
                         sys_scanner_class_t isa) {
  char buf[16];
  test_assert(sys_scanner_next(it));
  test_assert(it->bytes == bytes);
  test_assert(it->runes == 1 || isa != sys_scanner_control);
  test_assert(it->isa == isa);
  test_assert(sys_scanner_token(it, buf, sizeof(buf)) == bytes);
  test_assert(memcmp(buf, text, bytes) == 0);
}

test_main_sys(0) {
  char buf[32];

  ///////////////////////////////////////////////////////////////////////
  // The scanner: a zero byte is one control character

  {
    sys_iostream_t *s = open_bytes(buf, sizeof(buf), "ab\0cd", 5);
    sys_scanner_t it = sys_scanner_init(s, 0);
    expect_token(&it, "ab", 2, sys_scanner_alpha);
    expect_token(&it, "\0", 1, sys_scanner_control);
    expect_token(&it, "cd", 2, sys_scanner_alpha);
    test_assert(!sys_scanner_next(&it));
    sys_iostream_close(s);
  }

  // Two zero bytes are two tokens, and one can be last
  {
    sys_iostream_t *s = open_bytes(buf, sizeof(buf), "\0\0a\0", 4);
    sys_scanner_t it = sys_scanner_init(s, 0);
    expect_token(&it, "\0", 1, sys_scanner_control);
    expect_token(&it, "\0", 1, sys_scanner_control);
    expect_token(&it, "a", 1, sys_scanner_alpha);
    expect_token(&it, "\0", 1, sys_scanner_control);
    test_assert(!sys_scanner_next(&it));
    sys_iostream_close(s);
  }

  // A zero byte ends a date, a duration or a number
  {
    sys_iostream_t *s =
        open_bytes(buf, sizeof(buf), "2026-09-06\0" "5s\0" "42\0", 17);
    sys_scanner_t it = sys_scanner_init(
        s, sys_scanner_dates | sys_scanner_durations | sys_scanner_numbers);
    expect_token(&it, "2026-09-06", 10, sys_scanner_date);
    expect_token(&it, "\0", 1, sys_scanner_control);
    expect_token(&it, "5s", 2, sys_scanner_duration);
    expect_token(&it, "\0", 1, sys_scanner_control);
    expect_token(&it, "42", 2, sys_scanner_number);
    expect_token(&it, "\0", 1, sys_scanner_control);
    test_assert(!sys_scanner_next(&it));
    sys_iostream_close(s);
  }

  // A zero byte inside a quoted string is part of it
  {
    sys_iostream_t *s = open_bytes(buf, sizeof(buf), "\"a\0b\"c", 6);
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes);
    expect_token(&it, "\"a\0b\"", 5, sys_scanner_string);
    expect_token(&it, "c", 1, sys_scanner_alpha);
    test_assert(!sys_scanner_next(&it));
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // The rune tokenizer: zero bytes are a run of control characters

  {
    sys_iostream_t *s = open_bytes(buf, sizeof(buf), "ab\0\0cd", 6);
    sys_rune_tokenize_t it = sys_rune_tokenize_init(s);
    test_assert(sys_rune_tokenize_next(&it));
    test_assert(it.bytes == 2 && it.isa == sys_rune_alpha);
    test_assert(sys_rune_tokenize_next(&it));
    test_assert(it.bytes == 2 && it.runes == 2 && it.isa == sys_rune_control);
    test_assert(sys_rune_tokenize_next(&it));
    test_assert(it.bytes == 2 && it.isa == sys_rune_alpha);
    test_assert(!sys_rune_tokenize_next(&it));
    sys_iostream_close(s);
  }
}
