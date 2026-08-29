#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Remove leading and trailing whitespace from a string in-place.
 * @param str The string to trim.
 * @return str, for chaining, or NULL if the input was NULL.
 */
char *sys_string_trimspace(char *str) {
  if (str == NULL) {
    return NULL;
  }

  // Find where the leading whitespace ends, without moving anything yet.
  const char *p = str;
  rune_t r;
  const char *next;
  while ((next = sys_rune_next(p, &r)) != NULL && sys_rune_is_space(r)) {
    p = next;
  }
  const char *start = p;

  // Walk the remainder, remembering the position right after the last
  // non-space rune - trailing whitespace gets truncated there.
  const char *end = start;
  while ((next = sys_rune_next(p, &r)) != NULL) {
    p = next;
    if (!sys_rune_is_space(r)) {
      end = p;
    }
  }

  // Shift the trimmed content down to the start of str's own buffer -
  // safe as a plain forward copy since the destination index is never
  // ahead of the source index - and return str itself, not a pointer
  // into the middle of it.
  size_t n = (size_t)(end - start);
  for (size_t i = 0; i < n; i++) {
    str[i] = start[i];
  }
  str[n] = '\0';
  return str;
}

/**
 * @brief Remove a prefix from a string if it exists.
 * @param s The string to modify.
 * @param prefix The prefix to remove.
 * @return s, for chaining, or NULL if the input was NULL.
 */
char *sys_string_trimprefix(char *s, const char *prefix) {
  if (s == NULL) {
    return NULL;
  }
  if (sys_string_hasprefix(s, prefix)) {
    // Shift the remainder down to the start of s's own buffer - safe as
    // a plain forward copy since the destination index is never ahead
    // of the source index.
    size_t prefix_len = sys_string_bytes(prefix);
    size_t remaining = sys_string_bytes(s) - prefix_len;
    for (size_t i = 0; i < remaining; i++) {
      s[i] = s[prefix_len + i];
    }
    s[remaining] = '\0';
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
