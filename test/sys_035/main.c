#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_symbol - ASCII

  static const char symbols[] = "$+<=>^`|~";
  for (size_t i = 0; i < sizeof(symbols) - 1; i++) {
    test_assert(sys_rune_is_symbol((rune_t)(uint8_t)symbols[i]) == true);
  }
  test_assert(sys_rune_is_symbol('0') == false);
  test_assert(sys_rune_is_symbol('A') == false);
  test_assert(sys_rune_is_symbol('a') == false);
  test_assert(sys_rune_is_symbol(' ') == false);
  test_assert(sys_rune_is_symbol('!') == false); // punct, not symbol
  test_assert(sys_rune_is_symbol(0x7F) == false); // DEL, control not symbol

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_symbol - Latin-1 Supplement

  static const rune_t latin1_symbols[] = {
      0x00A2, // cent sign
      0x00A3, // pound sign
      0x00A4, // currency sign
      0x00A5, // yen sign
      0x00A6, // broken bar
      0x00A8, // diaeresis
      0x00A9, // copyright sign
      0x00AC, // not sign
      0x00AE, // registered sign
      0x00AF, // macron
      0x00B0, // degree sign
      0x00B1, // plus-minus sign
      0x00B4, // acute accent
      0x00B8, // cedilla
      0x00D7, // multiplication sign
      0x00F7, // division sign
  };
  for (size_t i = 0; i < sizeof(latin1_symbols) / sizeof(latin1_symbols[0]);
       i++) {
    test_assert(sys_rune_is_symbol(latin1_symbols[i]) == true);
  }

  // Neighbours that must NOT be symbols: Latin-1 punctuation and letters.
  test_assert(sys_rune_is_symbol(0x00A1) == false); // inverted !, is punct
  test_assert(sys_rune_is_symbol(0x00A7) == false); // section sign, is punct
  test_assert(sys_rune_is_symbol(0x00C0) == false); // A grave, is a letter
  test_assert(sys_rune_is_symbol(0x00A0) == false); // NBSP, is space

  ///////////////////////////////////////////////////////////////////////
  // Out-of-range / malformed values

  test_assert(sys_rune_is_symbol(RUNE_ERROR) == false);
  test_assert(sys_rune_is_symbol(-1) == false);
  test_assert(sys_rune_is_symbol(0x10FFFF) == false);
  test_assert(sys_rune_is_symbol(0x20AC) == false); // euro sign, out of Latin-1 scope

  ///////////////////////////////////////////////////////////////////////
  // Exhaustive partition check: every printable ASCII character
  // (0x21-0x7E, i.e. excluding space and DEL) must be classified as
  // exactly one of digit/alpha/punct/symbol - no gaps, no overlaps. This
  // is what guarantees is_symbol was carved out of is_punct correctly.

  for (rune_t r = 0x21; r <= 0x7E; r++) {
    int n = (sys_rune_is_digit(r) ? 1 : 0) + (sys_rune_is_alpha(r) ? 1 : 0) +
            (sys_rune_is_punct(r) ? 1 : 0) + (sys_rune_is_symbol(r) ? 1 : 0);
    test_assert(n == 1);
  }

}
