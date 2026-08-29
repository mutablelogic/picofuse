#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Determine the class of a rune.
 * @param r The rune to classify.
 * @return The class of the rune.
 */
sys_rune_class_t sys_rune_isa(rune_t r) {
  if (sys_rune_is_space(r)) {
    return sys_rune_space;
  }
  if (sys_rune_is_control(r)) {
    return sys_rune_control;
  }
  if (sys_rune_is_digit(r)) {
    return sys_rune_digit;
  }
  if (sys_rune_is_alpha(r)) {
    return sys_rune_alpha;
  }
  if (sys_rune_is_punct(r)) {
    return sys_rune_punct;
  }
  if (sys_rune_is_symbol(r)) {
    return sys_rune_symbol;
  }
  return sys_rune_other;
}
