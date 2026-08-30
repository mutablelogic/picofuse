#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_digit (ASCII only - Latin-1 has no additional digits)

  for (rune_t r = '0'; r <= '9'; r++) {
    test_assert(sys_rune_is_digit(r) == true);
  }
  test_assert(sys_rune_is_digit('/') == false); // just below '0'
  test_assert(sys_rune_is_digit(':') == false); // just above '9'
  test_assert(sys_rune_is_digit('a') == false);
  test_assert(sys_rune_is_digit(0x0660) == false); // Arabic-Indic digit zero

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_space

  test_assert(sys_rune_is_space(' ') == true);
  test_assert(sys_rune_is_space('\t') == true);
  test_assert(sys_rune_is_space('\n') == true);
  test_assert(sys_rune_is_space('\v') == true);
  test_assert(sys_rune_is_space('\f') == true);
  test_assert(sys_rune_is_space('\r') == true);
  test_assert(sys_rune_is_space(0x0085) == true); // NEL
  test_assert(sys_rune_is_space(0x00A0) == true); // NBSP
  test_assert(sys_rune_is_space('a') == false);
  test_assert(sys_rune_is_space(0x1C) == false);   // file separator, not "space"
  test_assert(sys_rune_is_space(0x0084) == false); // just below NEL
  test_assert(sys_rune_is_space(0x0086) == false); // just above NEL
  test_assert(sys_rune_is_space(0x009F) == false); // just below NBSP
  test_assert(sys_rune_is_space(0x00A1) == false); // just above NBSP

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_alpha / is_upper / is_lower - ASCII

  for (rune_t r = 'A'; r <= 'Z'; r++) {
    test_assert(sys_rune_is_alpha(r) == true);
    test_assert(sys_rune_is_upper(r) == true);
    test_assert(sys_rune_is_lower(r) == false);
  }
  for (rune_t r = 'a'; r <= 'z'; r++) {
    test_assert(sys_rune_is_alpha(r) == true);
    test_assert(sys_rune_is_lower(r) == true);
    test_assert(sys_rune_is_upper(r) == false);
  }

  // Boundaries immediately outside each ASCII letter range.
  test_assert(sys_rune_is_upper('@') == false); // just below 'A'
  test_assert(sys_rune_is_upper('[') == false); // just above 'Z'
  test_assert(sys_rune_is_lower('`') == false); // just below 'a'
  test_assert(sys_rune_is_lower('{') == false); // just above 'z'
  test_assert(sys_rune_is_alpha('9') == false);

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_alpha / is_upper / is_lower - Latin-1 Supplement

  for (rune_t r = 0x00C0; r <= 0x00D6; r++) { // A grave - O diaeresis
    test_assert(sys_rune_is_upper(r) == true);
    test_assert(sys_rune_is_alpha(r) == true);
    test_assert(sys_rune_is_lower(r) == false);
  }
  test_assert(sys_rune_is_upper(0x00D7) == false); // multiplication sign, not a letter
  for (rune_t r = 0x00D8; r <= 0x00DE; r++) { // O stroke - Thorn
    test_assert(sys_rune_is_upper(r) == true);
    test_assert(sys_rune_is_alpha(r) == true);
  }

  test_assert(sys_rune_is_lower(0x00DF) == true); // sharp s
  for (rune_t r = 0x00E0; r <= 0x00F6; r++) { // a grave - o diaeresis
    test_assert(sys_rune_is_lower(r) == true);
    test_assert(sys_rune_is_alpha(r) == true);
    test_assert(sys_rune_is_upper(r) == false);
  }
  test_assert(sys_rune_is_lower(0x00F7) == false); // division sign, not a letter
  for (rune_t r = 0x00F8; r <= 0x00FF; r++) { // o stroke - y diaeresis
    test_assert(sys_rune_is_lower(r) == true);
    test_assert(sys_rune_is_alpha(r) == true);
  }

  // Boundaries immediately outside each Latin-1 letter range.
  test_assert(sys_rune_is_upper(0x00BF) == false); // just below A grave
  test_assert(sys_rune_is_lower(0x00BF) == false);
  test_assert(sys_rune_is_upper(0x0100) == false); // just above y diaeresis
  test_assert(sys_rune_is_lower(0x0100) == false);

  // Fully out of scope: non-Latin-1 scripts report false, not a crash.
  test_assert(sys_rune_is_alpha(0x0391) == false); // Greek capital alpha
  test_assert(sys_rune_is_alpha(0x4E2D) == false); // CJK "middle"

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_punct - ASCII (Unicode Punctuation category, not the
  // broader C ispunct(): symbol-class characters are excluded)

  static const char punct[] = "!\"#%&'()*,-./:;?@[\\]_{}";
  for (size_t i = 0; i < sizeof(punct) - 1; i++) {
    test_assert(sys_rune_is_punct((rune_t)(uint8_t)punct[i]) == true);
  }

  // Symbol-class ASCII characters: NOT punctuation under this scheme,
  // unlike C's ispunct().
  static const char symbols[] = "$+<=>^`|~";
  for (size_t i = 0; i < sizeof(symbols) - 1; i++) {
    test_assert(sys_rune_is_punct((rune_t)(uint8_t)symbols[i]) == false);
  }

  test_assert(sys_rune_is_punct('0') == false);
  test_assert(sys_rune_is_punct('A') == false);
  test_assert(sys_rune_is_punct('a') == false);
  test_assert(sys_rune_is_punct(' ') == false);
  test_assert(sys_rune_is_punct(0x7F) == false); // DEL, control not punct

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_punct - Latin-1 Supplement

  test_assert(sys_rune_is_punct(0x00A1) == true); // inverted exclamation mark
  test_assert(sys_rune_is_punct(0x00A7) == true); // section sign
  test_assert(sys_rune_is_punct(0x00B6) == true); // pilcrow sign
  test_assert(sys_rune_is_punct(0x00B7) == true); // middle dot
  test_assert(sys_rune_is_punct(0x00BF) == true); // inverted question mark
  test_assert(sys_rune_is_punct(0x00A0) == false); // NBSP is space, not punct
  test_assert(sys_rune_is_punct(0x00C0) == false); // A grave is a letter

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_is_control

  for (rune_t r = 0x00; r <= 0x1F; r++) {
    test_assert(sys_rune_is_control(r) == true);
  }
  test_assert(sys_rune_is_control(0x7F) == true); // DEL
  test_assert(sys_rune_is_control(' ') == false); // just above 0x1F
  test_assert(sys_rune_is_control('~') == false); // just below DEL

  // C1 control range.
  for (rune_t r = 0x80; r <= 0x9F; r++) {
    test_assert(sys_rune_is_control(r) == true);
  }
  test_assert(sys_rune_is_control(0x7E) == false); // just below DEL ('~')
  test_assert(sys_rune_is_control(0xA0) == false); // NBSP, just above C1 range

  ///////////////////////////////////////////////////////////////////////
  // Out-of-range / malformed values must not crash and must classify as
  // "none of the above"

  test_assert(sys_rune_is_digit(RUNE_ERROR) == false);
  test_assert(sys_rune_is_space(RUNE_ERROR) == false);
  test_assert(sys_rune_is_alpha(RUNE_ERROR) == false);
  test_assert(sys_rune_is_upper(RUNE_ERROR) == false);
  test_assert(sys_rune_is_lower(RUNE_ERROR) == false);
  test_assert(sys_rune_is_punct(RUNE_ERROR) == false);
  test_assert(sys_rune_is_control(RUNE_ERROR) == false);

  test_assert(sys_rune_is_digit(-1) == false);
  test_assert(sys_rune_is_alpha(-1) == false);
  test_assert(sys_rune_is_control(-1) == false);

  test_assert(sys_rune_is_digit(0x10FFFF) == false); // max valid codepoint
  test_assert(sys_rune_is_alpha(0x10FFFF) == false);

  sys_exit();
  return 0;
}
