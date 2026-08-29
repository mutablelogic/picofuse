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
  // Without the flag, "//" is just two ordinary punct runes merging
  // into one punctuation run (same class), same as any other '/'.

  {
    sys_iostream_t *s = sys_string_read("//a");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_none);
    expect_token(&it, "//", 2, 2, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A comment with nothing after it (runs to end of stream).

  {
    sys_iostream_t *s = sys_string_read("// hello");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "// hello", 8, 8, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A bare "//" with nothing after it at all.
  {
    sys_iostream_t *s = sys_string_read("//");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "//", 2, 2, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // A single '/' not followed by a second one is not a comment - it
  // falls back to ordinary punctuation.

  {
    sys_iostream_t *s = sys_string_read("/a");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "/", 1, 1, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A single '/' at the very end of the stream.
  {
    sys_iostream_t *s = sys_string_read("/");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "/", 1, 1, sys_scanner_punct);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // A single '/' that fails to start a comment still merges normally
  // into a surrounding punctuation run - the failed lookahead doesn't
  // leave anything stuck or skipped.
  {
    sys_iostream_t *s = sys_string_read("!/a");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "!/", 2, 2, sys_scanner_punct);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // The comment stops right before '\n' - not consumed, left for the
  // next token to classify normally.

  {
    sys_iostream_t *s = sys_string_read("//hi\nx");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "//hi", 4, 4, sys_scanner_comment);
    expect_token(&it, "\n", 1, 1, sys_scanner_control);
    expect_token(&it, "x", 1, 1, sys_scanner_alpha);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // Composes correctly with sys_scanner_newlines.
  {
    sys_iostream_t *s = sys_string_read("//a\n//b");
    sys_scanner_t it = sys_scanner_init(
        s, sys_scanner_comments_slash | sys_scanner_newlines);
    expect_token(&it, "//a", 3, 3, sys_scanner_comment);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, "//b", 3, 3, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // "//" is never absorbed into a preceding punctuation run.

  {
    sys_iostream_t *s = sys_string_read("!//hi");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "!", 1, 1, sys_scanner_punct);
    expect_token(&it, "//hi", 4, 4, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Composes with nospace.

  {
    sys_iostream_t *s = sys_string_read("a //hi");
    sys_scanner_t it = sys_scanner_init(
        s, sys_scanner_comments_slash | sys_scanner_nospaces);
    expect_token(&it, "a", 1, 1, sys_scanner_alpha);
    expect_token(&it, "//hi", 4, 4, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Both comment styles together, without interfering with each other.

  {
    sys_iostream_t *s = sys_string_read("#a\n//b");
    sys_scanner_t it = sys_scanner_init(
        s, sys_scanner_comments_hash | sys_scanner_comments_slash |
               sys_scanner_newlines);
    expect_token(&it, "#a", 2, 2, sys_scanner_comment);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, "//b", 3, 3, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  // sys_scanner_comments is equivalent to OR-ing the two individual
  // flags together by hand.
  {
    sys_iostream_t *s = sys_string_read("#a\n//b");
    sys_scanner_t it =
        sys_scanner_init(s, sys_scanner_comments | sys_scanner_newlines);
    expect_token(&it, "#a", 2, 2, sys_scanner_comment);
    expect_token(&it, "\n", 1, 1, sys_scanner_newline);
    expect_token(&it, "//b", 3, 3, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Multi-byte UTF-8 content.

  {
    sys_iostream_t *s = sys_string_read("//caf\xC3\xA9");
    sys_scanner_t it = sys_scanner_init(s, sys_scanner_comments_slash);
    expect_token(&it, "//caf\xC3\xA9", 7, 6, sys_scanner_comment);
    test_assert(sys_scanner_next(&it) == false);
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
