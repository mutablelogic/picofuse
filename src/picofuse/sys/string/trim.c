#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Remove leading and trailing whitespace from a string in-place.
 * @param str The string to trim.
 * @return The modified string pointer, or NULL if the input was NULL.
 */
char *sys_string_trimspace(char *str) {
  if (str == NULL) {
    return NULL;
  }

  // Skip leading whitespace by advancing the pointer - the returned
  // string still shares str's buffer, so no bytes are moved for this
  // half of the trim.
  const char *p = str;
  rune_t r;
  const char *next;
  while ((next = sys_rune_next(p, &r)) != NULL && sys_rune_is_space(r)) {
    p = next;
  }
  char *start = (char *)p;

  // Walk the remainder, remembering the position right after the last
  // non-space rune - trailing whitespace gets truncated there.
  char *end = start;
  while ((next = sys_rune_next(p, &r)) != NULL) {
    p = next;
    if (!sys_rune_is_space(r)) {
      end = (char *)p;
    }
  }
  *end = '\0';
  return start;
}

/**
 * @brief Remove a prefix from a string if it exists.
 * @param s The string to modify.
 * @param prefix The prefix to remove.
 * @return The modified string pointer, or the original string if the prefix was
 * not present.
 */
char *sys_string_trimprefix(char *s, const char *prefix) {
  if (s == NULL) {
    return NULL;
  }
  if (sys_string_hasprefix(s, prefix)) {
    return s + sys_string_bytes(prefix);
  }
  return s;
}

/**
 * @brief Remove a suffix from a string if it exists.
 * @param s The string to modify.
 * @param suffix The suffix to remove.
 * @return The modified string pointer, or the original string if the suffix was
 * not present.
 */
char *sys_string_trimsuffix(char *s, const char *suffix) {
  if (s == NULL) {
    return NULL;
  }
  if (sys_string_hassuffix(s, suffix)) {
    size_t s_len = sys_string_bytes(s);
    size_t suffix_len = sys_string_bytes(suffix);
    s[s_len - suffix_len] = '\0';
  }
  return s;
}
