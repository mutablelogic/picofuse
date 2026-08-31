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

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // Without the flag, '\'' is ordinary punctuation.

  {
    sys_iostream_t *s = sys_string_read("'a'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "'", 1, 1, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, "'", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Basic quoted strings

  {
    sys_iostream_t *s = sys_string_read("'hello'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'hello'", 7, 7, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Empty string.
  {
    sys_iostream_t *s = sys_string_read("''");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "''", 2, 2, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Escapes leave the string open - \' doesn't end it, and it's kept
  // byte-for-byte in the token (not interpreted).

  {
    sys_iostream_t *s = sys_string_read("'it\\'s'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'it\\'s'", 7, 7, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // \\ has to be treated the same way \' is, or this would be
  // misparsed: without generic "backslash escapes whatever follows",
  // \\ then ' would look like an escaped quote and swallow the real
  // terminator, greedily absorbing everything after it too. Content is:
  // a, an escaped backslash, then the string properly ends - leaving
  // " rest" as separate tokens is exactly what a narrow \'-only
  // implementation gets wrong (it would swallow the closing quote as
  // "escaped" and keep consuming into " rest" looking for a real
  // terminator that never comes).
  {
    sys_iostream_t *s = sys_string_read("'a\\\\' rest");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'a\\\\'", 5, 5, sys_scanner_string);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "rest", 4, 4, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Unterminated strings: best-effort to end of stream, distinguishable
  // by checking whether the retrieved text's last byte is '\''.

  {
    sys_iostream_t *s = sys_string_read("'abc");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'abc", 4, 4, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A trailing lone backslash right at end-of-stream must not crash or
  // hang - there's simply nothing left to escape.
  {
    sys_iostream_t *s = sys_string_read("'abc\\");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'abc\\", 5, 5, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A bare opening quote with nothing after it at all.
  {
    sys_iostream_t *s = sys_string_read("'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'", 1, 1, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Multiple strings, and a quote is never absorbed into a preceding
  // punctuation run.

  {
    sys_iostream_t *s = sys_string_read("'a' 'b'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'a'", 3, 3, sys_scanner_string);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "'b'", 3, 3, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  {
    sys_iostream_t *s = sys_string_read("!'a'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "'a'", 3, 3, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Composes with surrounding whitespace, unaffected by the flag.
  {
    sys_iostream_t *s = sys_string_read("'a'  'b'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'a'", 3, 3, sys_scanner_string);
    expect_token(&it, "  ", 2, 2, sys_scanner_space);
    expect_token(&it, "'b'", 3, 3, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // squote's own escaping is independent of sys_scanner_escapes - it
  // works the same whether or not that flag is also set.

  {
    sys_iostream_t *s = sys_string_read("'a\\tb'");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single); // no escapes flag
    expect_token(&it, "'a\\tb'", 6, 6, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Both flags together: a top-level JSON escape token outside any
  // string, and a quoted string with its own independent escape,
  // without interfering with each other.
  {
    sys_iostream_t *s = sys_string_read("\\n 'a\\tb'");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_quotes_single | sys_scanner_escapes);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "'a\\tb'", 6, 6, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Multi-byte UTF-8 content: byte count and rune count differ.

  {
    sys_iostream_t *s = sys_string_read("'caf\xC3\xA9'"); // 'café'
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_quotes_single);
    expect_token(&it, "'caf\xC3\xA9'", 7, 6, sys_scanner_string);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

}
