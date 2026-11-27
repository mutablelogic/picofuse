#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// Unicode 16's White_Space, and the punctuation (P*) and symbols (S*) in
// the General Punctuation block, above Latin-1 - from Python's unicodedata
static bool want_space(rune_t r) {
  return r == 0x1680 || (r >= 0x2000 && r <= 0x200A) || r == 0x2028 ||
         r == 0x2029 || r == 0x202F || r == 0x205F || r == 0x3000;
}

static bool want_punct(rune_t r) {
  return (r >= 0x2010 && r <= 0x2027) || (r >= 0x2030 && r <= 0x2043) ||
         (r >= 0x2045 && r <= 0x2051) || (r >= 0x2053 && r <= 0x205E);
}

static bool want_symbol(rune_t r) { return r == 0x2044 || r == 0x2052; }

static void expect_trim(const char *str, const char *want) {
  char buf[32];
  test_assert(strlen(str) < sizeof(buf));
  strcpy(buf, str);
  test_assert(strcmp(sys_string_trimspace(buf), want) == 0);
}

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // Every rune above Latin-1

  for (rune_t r = 0x0100; r <= 0x10FFFF; r++) {
    test_assert(sys_rune_is_space(r) == want_space(r));
    test_assert(sys_rune_is_punct(r) == want_punct(r));
    test_assert(sys_rune_is_symbol(r) == want_symbol(r));
  }

  ///////////////////////////////////////////////////////////////////////
  // The classes from the issue

  test_assert(sys_rune_isa(0x2013) == sys_rune_punct); // en dash
  test_assert(sys_rune_isa(0x2014) == sys_rune_punct); // em dash
  for (rune_t r = 0x2018; r <= 0x201D; r++) {         // curly quotes
    test_assert(sys_rune_isa(r) == sys_rune_punct);
  }
  test_assert(sys_rune_isa(0x2022) == sys_rune_punct); // bullet
  test_assert(sys_rune_isa(0x2026) == sys_rune_punct); // ellipsis
  test_assert(sys_rune_isa(0x2003) == sys_rune_space); // em space
  test_assert(sys_rune_isa(0x3000) == sys_rune_space); // ideographic space
  test_assert(sys_rune_isa(0x2044) == sys_rune_symbol); // fraction slash

  // Format characters, such as zero width space, aren't spaces
  test_assert(sys_rune_isa(0x200B) == sys_rune_other);
  test_assert(sys_rune_isa(0x2060) == sys_rune_other); // word joiner

  ///////////////////////////////////////////////////////////////////////
  // Trimming

  expect_trim("\xE2\x80\x83" "a b\xE3\x80\x80", "a b");     // em, ideographic
  expect_trim("\xE2\x80\xAF\xE2\x80\xA8" "a", "a");          // narrow NBSP, LS
  expect_trim("\xE2\x80\x8B" "a", "\xE2\x80\x8B" "a");       // zero width space
  expect_trim("\xE2\x80\x94" "a\xE2\x80\x94", "\xE2\x80\x94" "a\xE2\x80\x94");
}
