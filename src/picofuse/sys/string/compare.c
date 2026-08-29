#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Compare two strings lexicographically.
 * @param a First string, or NULL.
 * @param b Second string, or NULL.
 * @return A negative value if a < b, 0 if a == b, or a positive value if a > b.
 */
ptrdiff_t sys_string_compare(const char *a, const char *b) {
  if (a == NULL) {
    a = "";
  }
  if (b == NULL) {
    b = "";
  }
  while (*a != '\0' && *a == *b) {
    a++;
    b++;
  }
  // Unsigned comparison, so bytes >= 0x80 sort correctly regardless of
  // whether char is signed on this platform.
  return (ptrdiff_t)(uint8_t)*a - (ptrdiff_t)(uint8_t)*b;
}

ptrdiff_t sys_string_contains(const char *s, const char *substr) {
  if (s == NULL) {
    s = "";
  }
  if (substr == NULL) {
    substr = "";
  }

  size_t substr_len = sys_string_bytes(substr);
  if (substr_len == 0) {
    return 0;
  }

  for (const char *p = s; *p != '\0'; p++) {
    size_t i = 0;
    while (i < substr_len && p[i] == substr[i]) {
      i++;
    }
    if (i == substr_len) {
      return (ptrdiff_t)(p - s);
    }
  }
  return -1;
}
