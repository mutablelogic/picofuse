#include <picofuse/sys.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Returns the value of c as a digit in the given base (2, 8, 10, or
// 16), or -1 if c isn't a valid digit for that base.
static inline int _sys_string_digit_value(char c, int base) {
  int v;
  if (c >= '0' && c <= '9') {
    v = c - '0';
  } else if (c >= 'a' && c <= 'f') {
    v = c - 'a' + 10;
  } else if (c >= 'A' && c <= 'F') {
    v = c - 'A' + 10;
  } else {
    return -1;
  }
  return (v < base) ? v : -1;
}

// Parses an optional sign followed by a decimal/hex/octal/binary digit
// run into an unsigned magnitude, without applying the sign - shared by
// sys_string_parse_int32/64, which each just check the result fits
// their own width. str must contain exactly this one number and nothing
// else (a float, or any other trailing content, is a parse error, not
// silently ignored); with len set, str need not be NUL-terminated, and
// nothing past str[len - 1] is read.
static bool _sys_string_parse_magnitude(const char *str, size_t len,
                                        bool *negative, uint64_t *magnitude) {
  if (str == NULL) {
    return false;
  }
  const char *end = (len != 0) ? (str + len) : (str + sys_string_bytes(str));
  const char *pos = str;

  *negative = false;
  if (pos < end && (*pos == '+' || *pos == '-')) {
    *negative = (*pos == '-');
    pos++;
  }
  if (pos >= end) {
    return false; // nothing but a sign, or nothing at all
  }

  int base = 10;
  if (*pos == '0') {
    if (pos + 1 < end && (pos[1] == 'x' || pos[1] == 'X')) {
      base = 16;
      pos += 2;
    } else if (pos + 1 < end && (pos[1] == 'b' || pos[1] == 'B')) {
      base = 2;
      pos += 2;
    } else if (pos + 1 < end && (pos[1] == 'o' || pos[1] == 'O')) {
      base = 8;
      pos += 2;
    } else if (pos + 1 < end && pos[1] >= '0' && pos[1] <= '7') {
      // Bare leading-zero octal (C style) - the loop below picks up the
      // leading '0' itself as the first octal digit, so pos isn't
      // advanced here.
      base = 8;
    }
    // Otherwise just "0" on its own, or a leading zero followed by a
    // non-octal decimal digit ("089") - stays plain decimal, handled by
    // the digit loop below same as any other number.
  }

  size_t ndigits = 0;
  uint64_t mag = 0;
  while (pos < end) {
    int v = _sys_string_digit_value(*pos, base);
    if (v < 0) {
      break;
    }
    if (mag > (UINT64_MAX - (uint64_t)v) / (uint64_t)base) {
      return false; // overflows even a uint64_t
    }
    mag = mag * (uint64_t)base + (uint64_t)v;
    ndigits++;
    pos++;
  }

  if (ndigits == 0) {
    return false; // a prefix ("0x", "0b", "0o") with no digit after it
  }
  if (pos != end) {
    return false; // trailing content - includes floats ('.'/'e'/'E')
  }

  *magnitude = mag;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

bool sys_string_parse_int32(const char *str, size_t len, int32_t *value) {
  bool negative;
  uint64_t mag;
  if (!_sys_string_parse_magnitude(str, len, &negative, &mag)) {
    return false;
  }
  if (negative) {
    if (mag > 2147483648ULL) { // -INT32_MIN, as an unsigned magnitude
      return false;
    }
    if (value) {
      *value = (mag == 2147483648ULL) ? INT32_MIN : -(int32_t)mag;
    }
  } else {
    if (mag > 2147483647ULL) { // INT32_MAX
      return false;
    }
    if (value) {
      *value = (int32_t)mag;
    }
  }
  return true;
}

bool sys_string_parse_int64(const char *str, size_t len, int64_t *value) {
  bool negative;
  uint64_t mag;
  if (!_sys_string_parse_magnitude(str, len, &negative, &mag)) {
    return false;
  }
  if (negative) {
    if (mag > 9223372036854775808ULL) { // -INT64_MIN, as an unsigned magnitude
      return false;
    }
    if (value) {
      *value = (mag == 9223372036854775808ULL) ? INT64_MIN : -(int64_t)mag;
    }
  } else {
    if (mag > 9223372036854775807ULL) { // INT64_MAX
      return false;
    }
    if (value) {
      *value = (int64_t)mag;
    }
  }
  return true;
}

bool sys_string_parse_uint32(const char *str, size_t len, uint32_t *value) {
  bool negative;
  uint64_t mag;
  if (!_sys_string_parse_magnitude(str, len, &negative, &mag)) {
    return false;
  }
  if (negative || mag > UINT32_MAX) {
    return false;
  }
  if (value) {
    *value = (uint32_t)mag;
  }
  return true;
}

bool sys_string_parse_uint64(const char *str, size_t len, uint64_t *value) {
  bool negative;
  uint64_t mag;
  if (!_sys_string_parse_magnitude(str, len, &negative, &mag)) {
    return false;
  }
  if (negative) {
    return false;
  }
  if (value) {
    *value = mag;
  }
  return true;
}
