#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_to_upper / sys_rune_to_lower - ASCII round trip

  for (rune_t lower = 'a'; lower <= 'z'; lower++) {
    rune_t upper = lower - ('a' - 'A');
    test_assert(sys_rune_to_upper(lower) == upper);
    test_assert(sys_rune_to_lower(upper) == lower);
    // Already-correct case is returned unchanged (idempotent).
    test_assert(sys_rune_to_upper(upper) == upper);
    test_assert(sys_rune_to_lower(lower) == lower);
  }

  // Non-letters pass through unchanged.
  test_assert(sys_rune_to_upper('5') == '5');
  test_assert(sys_rune_to_upper(' ') == ' ');
  test_assert(sys_rune_to_upper('!') == '!');
  test_assert(sys_rune_to_lower('5') == '5');
  test_assert(sys_rune_to_lower(' ') == ' ');
  test_assert(sys_rune_to_lower('!') == '!');

  ///////////////////////////////////////////////////////////////////////
  // Latin-1 Supplement round trip

  for (rune_t upper = 0x00C0; upper <= 0x00D6; upper++) { // A grave - O diaeresis
    rune_t lower = upper + 0x20;
    test_assert(sys_rune_to_lower(upper) == lower);
    test_assert(sys_rune_to_upper(lower) == upper);
  }
  for (rune_t upper = 0x00D8; upper <= 0x00DE; upper++) { // O stroke - Thorn
    rune_t lower = upper + 0x20;
    test_assert(sys_rune_to_lower(upper) == lower);
    test_assert(sys_rune_to_upper(lower) == upper);
  }

  // Multiplication/division signs sit between the letter ranges and have
  // no case at all - unchanged by both directions.
  test_assert(sys_rune_to_lower(0x00D7) == 0x00D7); // multiplication sign
  test_assert(sys_rune_to_upper(0x00D7) == 0x00D7);
  test_assert(sys_rune_to_lower(0x00F7) == 0x00F7); // division sign
  test_assert(sys_rune_to_upper(0x00F7) == 0x00F7);

  // Sharp s and y diaeresis are lowercase letters (sys_rune_is_lower is
  // true) but have no single-codepoint uppercase form in this range, so
  // to_upper leaves them unchanged.
  test_assert(sys_rune_is_lower(0x00DF) == true);
  test_assert(sys_rune_to_upper(0x00DF) == 0x00DF); // sharp s
  test_assert(sys_rune_is_lower(0x00FF) == true);
  test_assert(sys_rune_to_upper(0x00FF) == 0x00FF); // y diaeresis

  ///////////////////////////////////////////////////////////////////////
  // Out-of-range / malformed values pass through unchanged, no crash

  test_assert(sys_rune_to_upper(RUNE_ERROR) == RUNE_ERROR);
  test_assert(sys_rune_to_lower(RUNE_ERROR) == RUNE_ERROR);
  test_assert(sys_rune_to_upper(-1) == -1);
  test_assert(sys_rune_to_lower(-1) == -1);
  test_assert(sys_rune_to_upper(0x10FFFF) == 0x10FFFF);
  test_assert(sys_rune_to_lower(0x10FFFF) == 0x10FFFF);

  // Fully out-of-scope scripts pass through unchanged rather than being
  // (incorrectly) treated as ASCII/Latin-1.
  test_assert(sys_rune_to_lower(0x0391) == 0x0391); // Greek capital alpha
  test_assert(sys_rune_to_upper(0x0430) == 0x0430); // Cyrillic small a

}
