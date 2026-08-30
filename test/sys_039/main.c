#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  // NULL is treated as "".
  test_assert(sys_string_compare(NULL, NULL) == 0);
  test_assert(sys_string_compare(NULL, "") == 0);
  test_assert(sys_string_compare("", NULL) == 0);
  test_assert(sys_string_compare(NULL, "a") < 0);
  test_assert(sys_string_compare("a", NULL) > 0);

  test_assert(sys_string_compare("", "") == 0);
  test_assert(sys_string_compare("abc", "abc") == 0);

  // Divergence at a shared prefix.
  test_assert(sys_string_compare("abc", "abd") < 0);
  test_assert(sys_string_compare("abd", "abc") > 0);

  // Prefix vs. longer string: strcmp-style ('\0' sorts before any byte).
  test_assert(sys_string_compare("ab", "abc") < 0);
  test_assert(sys_string_compare("abc", "ab") > 0);

  // Case sensitivity: uppercase sorts before lowercase in ASCII.
  test_assert(sys_string_compare("A", "a") < 0);
  test_assert(sys_string_compare("Z", "a") < 0); // 'Z' (0x5A) < 'a' (0x61)

  // Bytes >= 0x80 must sort as unsigned, not as negative signed chars -
  // this is the case a naive `*a - *b` on plain (possibly signed) char
  // gets backwards.
  test_assert(sys_string_compare("\xFF", "\x01") > 0);
  test_assert(sys_string_compare("\x01", "\xFF") < 0);
  test_assert(sys_string_compare("\x7F", "\x80") < 0);

  // Well-formed UTF-8 byte order matches codepoint order: an ASCII byte
  // (< 0x80) always sorts before any Latin-1 Supplement rune's 2-byte
  // encoding (which starts with 0xC2-0xC3), with no decoding needed.
  test_assert(sys_string_compare("z", "\xC3\x80") < 0); // 'z' vs "A grave"

  // Comparison stops at the first divergence; nothing beyond it matters,
  // including whichever string happens to be malformed there.
  test_assert(sys_string_compare("ab\x80" "cd", "ab\x80" "ce") < 0);

  sys_exit();
  return 0;
}
