#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

bool sys_string_hasprefix(const char *s, const char *prefix) {
  if (s == NULL) {
    s = "";
  }
  if (prefix == NULL) {
    prefix = "";
  }
  while (*prefix != '\0') {
    if (*s != *prefix) {
      return false;
    }
    s++;
    prefix++;
  }
  return true;
}

bool sys_string_hassuffix(const char *s, const char *suffix) {
  if (s == NULL) {
    s = "";
  }
  if (suffix == NULL) {
    suffix = "";
  }

  size_t s_len = sys_string_bytes(s);
  size_t suffix_len = sys_string_bytes(suffix);
  if (suffix_len > s_len) {
    return false;
  }

  const char *tail = s + (s_len - suffix_len);
  for (size_t i = 0; i < suffix_len; i++) {
    if (tail[i] != suffix[i]) {
      return false;
    }
  }
  return true;
}
