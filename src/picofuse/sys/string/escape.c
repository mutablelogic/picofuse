#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static inline bool _sys_string_ishex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static inline int _sys_string_hexval(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return c - 'A' + 10;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

bool sys_string_parse_escape(const char *str, size_t len, rune_t *rune) {
  if (str == NULL || str[0] != '\\' || (len != 0 && len < 2)) {
    if (rune) {
      *rune = RUNE_ERROR;
    }
    return false;
  }

  size_t want_len = 2; // every case but \uXXXX is a 2-byte escape
  rune_t value;

  switch (str[1]) {
  case '"':
    value = '"';
    break;
  case '\\':
    value = '\\';
    break;
  case '/':
    value = '/';
    break;
  case 'b':
    value = '\b';
    break;
  case 'f':
    value = '\f';
    break;
  case 'n':
    value = '\n';
    break;
  case 'r':
    value = '\r';
    break;
  case 't':
    value = '\t';
    break;
  case 'u': {
    want_len = 6;
    // Checked before reading the 4 hex digits, not after: with len set,
    // str isn't guaranteed to be NUL-terminated, so this has to happen
    // before str[2..5] are read at all. With len == 0, str is
    // NUL-terminated, and the digit loop below is naturally safe -
    // _sys_string_ishex('\0') is false, so a premature terminator just
    // fails the loop rather than reading past it.
    if (len != 0 && len != want_len) {
      if (rune) {
        *rune = RUNE_ERROR;
      }
      return false;
    }
    rune_t v = 0;
    for (int i = 0; i < 4; i++) {
      char c = str[2 + i];
      if (!_sys_string_ishex(c)) {
        if (rune) {
          *rune = RUNE_ERROR;
        }
        return false;
      }
      v = (v << 4) | _sys_string_hexval(c);
    }
    if (v >= 0xD800 && v <= 0xDFFF) {
      // A lone UTF-16 surrogate - not a valid standalone rune.
      if (rune) {
        *rune = RUNE_ERROR;
      }
      return false;
    }
    value = v;
    break;
  }
  default:
    if (rune) {
      *rune = RUNE_ERROR;
    }
    return false;
  }

  // Nothing may follow the escape: with len set, len must equal
  // want_len exactly (checked here for the single-character escapes;
  // the \uXXXX case above already checked it before reading its
  // digits). With len == 0, str is NUL-terminated, so the byte right
  // after the escape - always safe to read, since every byte of the
  // escape itself is confirmed non-NUL by this point - must be the
  // terminator itself.
  bool exact = (len != 0) ? (len == want_len) : (str[want_len] == '\0');
  if (!exact) {
    if (rune) {
      *rune = RUNE_ERROR;
    }
    return false;
  }

  if (rune) {
    *rune = value;
  }
  return true;
}
