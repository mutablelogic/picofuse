#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Check if a string contains only valid runes.
 * @param str The string to check.
 * @return true if the string is valid, false otherwise.
 */
bool sys_rune_valid(const char *str) {
  rune_t rune;
  const char *next;
  while ((next = sys_rune_next(str, &rune)) != NULL) {
    // A genuine decode error always advances by exactly 1 byte (see
    // sys_rune_next's doc comment) - a legitimately-encoded U+FFFD always
    // advances by 3, so this can't misfire on a real replacement
    // character in the input.
    if (rune == RUNE_ERROR && next == str + 1) {
      return false;
    }
    str = next;
  }
  return true;
}
