#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Parse "true" or "false" into a bool.
 * @param str String to parse, or NULL.
 * @param out Pointer to store the result. Left unchanged on a parse
 * error.
 * @return true if str is exactly "true" or "false" (case-sensitive,
 * nothing else in str), false otherwise.
 */
bool sys_string_parse_bool(const char *str, bool *out) {
  if (sys_string_compare(str, "true") == 0) {
    if (out) {
      *out = true;
    }
    return true;
  }
  if (sys_string_compare(str, "false") == 0) {
    if (out) {
      *out = false;
    }
    return true;
  }
  return false;
}
