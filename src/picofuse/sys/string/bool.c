#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Parse "true" or "false" into a bool.
 * @param str String to parse, or NULL.
 * @param len Length of the string, or 0 for its length up to the NUL
 * terminator.
 * @param out Pointer to store the result. Left unchanged on a parse
 * error.
 * @return true if str is exactly "true" or "false" (case-sensitive,
 * nothing else in str), false otherwise.
 */
bool sys_string_parse_bool(const char *str, size_t len, bool *out) {
  if (str == NULL) {
    return false;
  }
  if (len == 0) {
    len = strlen(str);
  }

  // Only compared when the length matches, so nothing past len is read
  bool value;
  if (len == 4 && memcmp(str, "true", 4) == 0) {
    value = true;
  } else if (len == 5 && memcmp(str, "false", 5) == 0) {
    value = false;
  } else {
    return false;
  }
  if (out) {
    *out = value;
  }
  return true;
}
