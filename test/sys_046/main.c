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

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // Without the flag, '\' is ordinary punctuation - unaffected by this
  // feature's existence.

  {
    sys_iostream_t *s = sys_string_read("\\n");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "\\", 1, 1, sys_scanner_punct);
    expect_token(&it, "n", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Every single-character JSON escape

  {
    sys_iostream_t *s = sys_string_read("\\\"\\\\\\/\\b\\f\\n\\r\\t");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\\"", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\\\", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\/", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\b", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\f", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\r", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\t", 2, 2, sys_scanner_escape);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // \uXXXX - exactly 4 hex digits, either case

  {
    sys_iostream_t *s = sys_string_read("\\u0041\\uABCD\\uabcd\\uAbC1");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\u0041", 6, 6, sys_scanner_escape);
    expect_token(&it, "\\uABCD", 6, 6, sys_scanner_escape);
    expect_token(&it, "\\uabcd", 6, 6, sys_scanner_escape);
    expect_token(&it, "\\uAbC1", 6, 6, sys_scanner_escape);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed \u falls back to ordinary tokenization instead of being
  // recognized as an escape

  {
    // Only 2 hex digits, then the stream ends.
    sys_iostream_t *s = sys_string_read("\\u12");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\", 1, 1, sys_scanner_punct);
    expect_token(&it, "u", 1, 1, sys_scanner_alpha);
    expect_token(&it, "12", 2, 2, sys_scanner_digit);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }
  {
    // 'u' followed by non-hex letters - falls back, and "uZZZZ" merges
    // into one ordinary alpha run since they're all letters.
    sys_iostream_t *s = sys_string_read("\\uZZZZ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\", 1, 1, sys_scanner_punct);
    expect_token(&it, "uZZZZ", 5, 5, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }
  {
    // '\u' right at the end of the stream - must not read past the end
    // while checking for 4 hex digits.
    sys_iostream_t *s = sys_string_read("\\u");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\", 1, 1, sys_scanner_punct);
    expect_token(&it, "u", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }
  {
    // A lone '\' at the very end of the stream.
    sys_iostream_t *s = sys_string_read("\\");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }
  {
    // An unrecognized escape character falls back too.
    sys_iostream_t *s = sys_string_read("\\q");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\", 1, 1, sys_scanner_punct);
    expect_token(&it, "q", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A '\' that starts a recognized escape is its own token, distinct
  // from a neighboring punctuation character - though since punctuation
  // is always a single rune anyway (see sys_scanner_class_t), this isn't
  // a special case, just the ordinary rule.

  {
    sys_iostream_t *s = sys_string_read(",\\n");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }
  {
    sys_iostream_t *s = sys_string_read(",\\n,");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A '\' that does NOT start a recognized escape is still its own
  // single-rune punctuation token, same as any other punctuation
  // character - it doesn't merge with a neighboring one.
  {
    sys_iostream_t *s = sys_string_read(",\\q");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, ",", 1, 1, sys_scanner_punct);
    expect_token(&it, "\\", 1, 1, sys_scanner_punct);
    expect_token(&it, "q", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Consecutive escapes, and an escape adjacent to a letter run

  {
    sys_iostream_t *s = sys_string_read("\\n\\t");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    expect_token(&it, "\\t", 2, 2, sys_scanner_escape);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }
  {
    sys_iostream_t *s = sys_string_read("ab\\u0041cd");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, "ab", 2, 2, sys_scanner_alpha);
    expect_token(&it, "\\u0041", 6, 6, sys_scanner_escape);
    expect_token(&it, "cd", 2, 2, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Composes correctly with surrounding whitespace, which is unaffected
  // by the flag - each space run is still its own token.

  {
    sys_iostream_t *s = sys_string_read(" \\n ");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_escapes);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
