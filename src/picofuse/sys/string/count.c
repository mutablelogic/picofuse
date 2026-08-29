#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

size_t sys_rune_count(const char *str) {
  size_t count = 0;
  rune_t rune;
  while ((str = sys_rune_next(str, &rune)) != NULL) {
    count++;
  }
  return count;
}

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
