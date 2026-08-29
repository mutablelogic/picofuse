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
  // Without the flag, '"' is ordinary punctuation.

  {
    sys_iostream_t *s = sys_string_read("\"a\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "\"", 1, 1, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, "\"", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Basic quoted strings

  {
    sys_iostream_t *s = sys_string_read("\"hello\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"hello\"", 7, 7, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("\"\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"\"", 2, 2, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // \" doesn't terminate, and \\ has to be treated the same generic way
  // as squote's \\ - see sys_050 for the full reasoning.

  {
    sys_iostream_t *s = sys_string_read("\"it\\\"s\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"it\\\"s\"", 7, 7, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("\"a\\\\\" rest");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"a\\\\\"", 5, 5, sys_scanner_string);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "rest", 4, 4, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Unterminated: best-effort to end of stream.

  {
    sys_iostream_t *s = sys_string_read("\"abc");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"abc", 4, 4, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("\"abc\\");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"abc\\", 5, 5, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"", 1, 1, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Never absorbed into a preceding punctuation run, and composes with
  // nospace/escapes.

  {
    sys_iostream_t *s = sys_string_read("!\"a\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "\"a\"", 3, 3, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("\"a\"  \"b\"");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_quotes_double | sys_scanner_nospaces);
    expect_token(&it, "\"a\"", 3, 3, sys_scanner_string);
    expect_token(&it, "\"b\"", 3, 3, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("\\n \"a\\tb\"");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_quotes_double | sys_scanner_escapes);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "\"a\\tb\"", 6, 6, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Multi-byte UTF-8 content.

  {
    sys_iostream_t *s = sys_string_read("\"caf\xC3\xA9\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_double);
    expect_token(&it, "\"caf\xC3\xA9\"", 7, 6, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Both flags together: each string only closes on its own matching
  // quote - the other quote character is ordinary content inside it.

  {
    sys_iostream_t *s = sys_string_read("'a\"b' \"c'd\"");
    sys_scanner_t it = sys_scanner_init(
        s, sys_scanner_quotes_single | sys_scanner_quotes_double);
    expect_token(&it, "'a\"b'", 5, 5, sys_scanner_string);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "\"c'd\"", 5, 5, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // sys_scanner_quotes is equivalent to OR-ing the two individual flags
  // together by hand.
  {
    sys_iostream_t *s = sys_string_read("'a\"b' \"c'd\"");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes);
    expect_token(&it, "'a\"b'", 5, 5, sys_scanner_string);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "\"c'd\"", 5, 5, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
