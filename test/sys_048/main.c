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

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // Without the flag, '\n' classifies the same way any other whitespace
  // does - sys_rune_isa() resolves it to sys_rune_space, not control
  // (see sys_rune_isa()'s doc comment) - so it merges into the same
  // sys_scanner_space run as any surrounding spaces, indistinguishable
  // from them. This is exactly what the flag exists to change.

  {
    sys_iostream_t *s = sys_string_read("a \n b");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, " \n ", 3, 3, sys_scanner_space);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Consecutive newlines merge into the same space run as any other
  // whitespace, without the flag.
  {
    sys_iostream_t *s = sys_string_read("\n\n\n");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "\n\n\n", 3, 3, sys_scanner_space);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // With the flag, '\n' is its own class - never merged with ordinary
  // whitespace on either side.

  {
    sys_iostream_t *s = sys_string_read("a \n b");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_newlines);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, " ", 1, 1, sys_scanner_space);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Consecutive newlines never merge - each '\n' is its own token, so
  // counting how many occurred is just counting sys_scanner_newline
  // tokens (unlike sys_scanner_space, sys_scanner_newline is never a
  // run - see sys_scanner_class_t).
  {
    sys_iostream_t *s = sys_string_read("\n\n\n");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_newlines);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Leading and trailing newlines are their own tokens too.
  {
    sys_iostream_t *s = sys_string_read("\nhello\n");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_newlines);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, "hello", 5, 5, sys_scanner_alpha);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // '\r' is unaffected by the flag - it stays sys_scanner_space (only
  // '\n' itself is singled out). With the flag, "\r\n" is still two
  // tokens (the '\r' can't merge into the following '\n' - they're
  // different classes now), rather than merging into a single space run
  // the way it would without the flag.
  {
    sys_iostream_t *s = sys_string_read("a\r\nb");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_newlines);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, "\r", 1, 1, sys_scanner_space);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Without the flag, "\r\n" merges into one ordinary space run.
  {
    sys_iostream_t *s = sys_string_read("a\r\nb");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, "\r\n", 2, 2, sys_scanner_space);
    expect_token(&it, "b", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Composes with sys_scanner_escapes without interference: a JSON
  // "\n" escape (backslash + 'n', 2 bytes) is a completely different
  // thing from a real 0x0A newline byte.

  {
    sys_iostream_t *s = sys_string_read("\\n\n");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_escapes | sys_scanner_newlines);
    expect_token(&it, "\\n", 2, 2, sys_scanner_escape);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

}
