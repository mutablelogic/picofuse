#include <picofuse/sys.h>
#include <test/test.h>

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // One representative rune per class

  test_assert(sys_rune_isa(' ') == sys_rune_space);
  test_assert(sys_rune_isa('5') == sys_rune_digit);
  test_assert(sys_rune_isa('A') == sys_rune_alpha);
  test_assert(sys_rune_isa(0x00C0) == sys_rune_alpha); // A grave
  test_assert(sys_rune_isa('!') == sys_rune_punct);
  test_assert(sys_rune_isa(0x00A1) == sys_rune_punct); // inverted !
  test_assert(sys_rune_isa('$') == sys_rune_symbol);
  test_assert(sys_rune_isa(0x00D7) == sys_rune_symbol); // multiplication sign
  test_assert(sys_rune_isa(0x01) == sys_rune_control);
  test_assert(sys_rune_isa(0x7F) == sys_rune_control); // DEL
  test_assert(sys_rune_isa(0x9F) == sys_rune_control); // last C1 control

  ///////////////////////////////////////////////////////////////////////
  // Out of scope / malformed -> sys_rune_other

  test_assert(sys_rune_isa(RUNE_ERROR) == sys_rune_other);
  test_assert(sys_rune_isa(0x0391) == sys_rune_other); // Greek capital alpha
  test_assert(sys_rune_isa(0x4E2D) == sys_rune_other); // CJK "middle"
  test_assert(sys_rune_isa(-1) == sys_rune_other);
  test_assert(sys_rune_isa(0x10FFFF) == sys_rune_other);

  ///////////////////////////////////////////////////////////////////////
  // NEL (U+0085) is both sys_rune_is_space() and sys_rune_is_control()
  // (it's in the C1 control range 0x80-0x9F). The documented check order
  // for sys_rune_isa() is control-before-space, so it resolves to
  // sys_rune_control here, not sys_rune_space - this is the one rune in
  // the whole ASCII/Latin-1 range where the two predicates disagree.
  test_assert(sys_rune_is_space(0x0085) == true);
  test_assert(sys_rune_is_control(0x0085) == true);
  test_assert(sys_rune_isa(0x0085) == sys_rune_control);

  ///////////////////////////////////////////////////////////////////////
  // Exhaustive cross-check against the individual predicates, in the
  // documented priority order, across the full ASCII/Latin-1 range.

  for (rune_t r = 0x00; r <= 0xFF; r++) {
    sys_rune_class_t expect;
    if (sys_rune_is_control(r)) {
      expect = sys_rune_control;
    } else if (sys_rune_is_space(r)) {
      expect = sys_rune_space;
    } else if (sys_rune_is_digit(r)) {
      expect = sys_rune_digit;
    } else if (sys_rune_is_alpha(r)) {
      expect = sys_rune_alpha;
    } else if (sys_rune_is_punct(r)) {
      expect = sys_rune_punct;
    } else if (sys_rune_is_symbol(r)) {
      expect = sys_rune_symbol;
    } else {
      expect = sys_rune_other;
    }
    test_assert(sys_rune_isa(r) == expect);
  }

  sys_exit();
  return 0;
}
