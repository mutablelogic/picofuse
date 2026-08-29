#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // NULL / not a quote at all

  {
    char buf[16];
    test_assert(sys_string_parse_quoted(NULL, 0, buf, sizeof(buf)) == -1);
  }
  {
    char buf[16];
    test_assert(sys_string_parse_quoted("hello", 0, buf, sizeof(buf)) == -1);
  }

  ///////////////////////////////////////////////////////////////////////
  // Empty quoted strings

  {
    char buf[16] = {0};
    test_assert(sys_string_parse_quoted("\"\"", 0, buf, sizeof(buf)) == 0);
  }
  {
    char buf[16] = {0};
    test_assert(sys_string_parse_quoted("''", 0, buf, sizeof(buf)) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Plain content, no escapes

  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"hello\"", 0, buf, sizeof(buf));
    test_assert(n == 5);
    test_assert(memcmp(buf, "hello", 5) == 0);
  }
  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("'hello'", 0, buf, sizeof(buf));
    test_assert(n == 5);
    test_assert(memcmp(buf, "hello", 5) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Every single-character JSON escape, inside a double-quoted string

  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", 0,
                                          buf, sizeof(buf));
    test_assert(n == 8);
    test_assert(memcmp(buf, "\"\\/\b\f\n\r\t", 8) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // \' is recognized (not part of sys_string_parse_escape()'s table),
  // needed to escape a literal quote - works in either quote style.

  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"a\\'b\"", 0, buf, sizeof(buf));
    test_assert(n == 3);
    test_assert(memcmp(buf, "a'b", 3) == 0);
  }
  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("'a\\'b'", 0, buf, sizeof(buf));
    test_assert(n == 3);
    test_assert(memcmp(buf, "a'b", 3) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // The other quote character is ordinary content, not a terminator or
  // an escape - each string only closes on its own matching quote.

  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("'a\"b'", 0, buf, sizeof(buf));
    test_assert(n == 3);
    test_assert(memcmp(buf, "a\"b", 3) == 0);
  }
  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"a'b\"", 0, buf, sizeof(buf));
    test_assert(n == 3);
    test_assert(memcmp(buf, "a'b", 3) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // \uXXXX - ASCII, and codepoints needing 2 or 3 UTF-8 bytes

  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"\\u0041\"", 0, buf, sizeof(buf));
    test_assert(n == 1);
    test_assert(memcmp(buf, "A", 1) == 0);
  }
  {
    // \u00e9 - e acute, 2 UTF-8 bytes.
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"\\u00e9\"", 0, buf, sizeof(buf));
    test_assert(n == 2);
    test_assert(memcmp(buf, "\xC3\xA9", 2) == 0);
  }
  {
    // \u4e2d - CJK "middle", 3 UTF-8 bytes.
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"\\u4e2d\"", 0, buf, sizeof(buf));
    test_assert(n == 3);
    test_assert(memcmp(buf, "\xE4\xB8\xAD", 3) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Ordinary multi-byte UTF-8 content passes through unchanged.

  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"caf\xC3\xA9\"", 0, buf, sizeof(buf));
    test_assert(n == 5);
    test_assert(memcmp(buf, "caf\xC3\xA9", 5) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // The classic "escaped backslash right before the real closing quote"
  // case - \\ has to be treated the same generic way as any other
  // escape, or this misreads as an escaped quote and never closes.

  {
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("'a\\\\'", 0, buf, sizeof(buf));
    test_assert(n == 2);
    test_assert(memcmp(buf, "a\\", 2) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // str and out may be the same buffer - decoding never expands content
  // (every escape's decoded form is no longer than the escape itself),
  // so writing into str's own buffer as it's read never clobbers a byte
  // before it's been consumed. Exercises all three shrink amounts in one
  // pass: \' (2->1), ordinary content (1->1), \uXXXX (6->2).

  {
    char buf[] = "\"a\\'bc\\u00e9\"";
    ptrdiff_t n = sys_string_parse_quoted(buf, 0, buf, sizeof(buf));
    test_assert(n == 6);
    test_assert(memcmp(buf, "a'bc\xC3\xA9", 6) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed content - all parse errors

  {
    char buf[16] = {0};
    // Not a recognized escape character (not JSON's table, not \').
    test_assert(sys_string_parse_quoted("\"a\\qb\"", 0, buf, sizeof(buf)) == -1);
  }
  {
    char buf[16] = {0};
    // Malformed \u.
    test_assert(sys_string_parse_quoted("\"\\uZZZZ\"", 0, buf, sizeof(buf)) == -1);
  }
  {
    char buf[16] = {0};
    // Trailing backslash, nothing to escape.
    test_assert(sys_string_parse_quoted("\"abc\\", 0, buf, sizeof(buf)) == -1);
  }
  {
    char buf[16] = {0};
    // No closing quote at all.
    test_assert(sys_string_parse_quoted("\"abc", 0, buf, sizeof(buf)) == -1);
  }

  ///////////////////////////////////////////////////////////////////////
  // len == 0: str's own NUL terminator stands in for len - trailing
  // content after the closing quote is a parse error, not ignored.

  {
    char buf[16] = {0};
    test_assert(sys_string_parse_quoted("\"a\" extra", 0, buf, sizeof(buf)) == -1);
  }

  ///////////////////////////////////////////////////////////////////////
  // len > 0: the quoted string must consume exactly len bytes.

  {
    // The exact span of the token itself still succeeds, regardless of
    // what follows in the underlying buffer.
    char buf[16] = {0};
    ptrdiff_t n = sys_string_parse_quoted("\"hi\" more", 4, buf, sizeof(buf));
    test_assert(n == 2);
    test_assert(memcmp(buf, "hi", 2) == 0);
  }
  {
    // Too short: the closing quote isn't reached within len bytes.
    char buf[16] = {0};
    test_assert(sys_string_parse_quoted("\"hi\"", 3, buf, sizeof(buf)) == -1);
  }
  {
    // Too long: the closing quote comes before len bytes are used.
    char buf[16] = {0};
    test_assert(sys_string_parse_quoted("\"hi\" x", 5, buf, sizeof(buf)) == -1);
  }

  ///////////////////////////////////////////////////////////////////////
  // cap truncation - silent, matching sys_scanner_token()'s convention.

  {
    char buf[5];
    ptrdiff_t n = sys_string_parse_quoted("\"hello world\"", 0, buf, sizeof(buf));
    test_assert(n == 5);
    test_assert(memcmp(buf, "hello", 5) == 0);
  }

  // cap == 0 with out == NULL is safe - min(actual, cap) is 0, same as
  // sys_scanner_token() with cap == 0, not the true decoded length.
  {
    test_assert(sys_string_parse_quoted("\"hello\"", 0, NULL, 0) == 0);
  }

  // out == NULL with a nonzero (inconsistent, but not the caller's
  // fault to get right) cap doesn't crash - cap is ignored entirely
  // whenever out is NULL, same as if it had been passed as 0.
  {
    test_assert(sys_string_parse_quoted("\"hello\"", 0, NULL, 64) == 0);
  }

  sys_exit();
  return 0;
}
