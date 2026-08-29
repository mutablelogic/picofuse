#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Count the number of runes (Unicode code points) in a string.
 * @param str String to count runes in, or NULL.
 * @return The number of runes in the string.
 */
size_t sys_rune_count(const char *str) {
  size_t count = 0;
  rune_t rune;
  while ((str = sys_rune_next(str, &rune)) != NULL) {
    count++;
  }
  return count;
}
/**
 * @brief Count the number of bytes in a string.
 * @param str String to count bytes in, or NULL.
 * @return The number of bytes in the string.
 */
size_t sys_string_bytes(const char *str) {
  if (str == NULL) {
    return 0;
  }
  size_t n = 0;
  while (str[n] != '\0') {
    n++;
  }
  return n;
}
