#include <math.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Beyond this many decimal places of magnitude, a double has already
// overflowed to infinity or underflowed to 0 regardless of further
// digits - clamping the exponent here keeps parsing O(digits) without
// needing arbitrary-precision arithmetic for a pathological exponent
// like "1e999999999999".
#define _SYS_STRING_FLOAT_EXP_LIMIT 400

// Computes 10^exp as a double via exponentiation by squaring - O(log
// |exp|) multiplications regardless of magnitude, so an extreme exponent
// can't cause a slow loop. IEEE754 overflow/underflow naturally produces
// +-infinity/0 as the squaring proceeds, with no special-casing needed.
static inline double _sys_string_pow10(long exp) {
  bool neg = exp < 0;
  unsigned long n = neg ? (unsigned long)(-exp) : (unsigned long)exp;
  double base = 10.0;
  double result = 1.0;
  while (n > 0) {
    if (n & 1) {
      result *= base;
    }
    base *= base;
    n >>= 1;
  }
  return neg ? 1.0 / result : result;
}

// Shared by sys_string_parse_float32/64, which each just narrow the
// result to their own width. Always accumulates in double precision -
// very long digit sequences (beyond a double's own ~17 significant
// decimal digits) won't be bit-exact, though the value will still be
// close. Recognizes NaN and (optionally signed) Inf as exact-spelling
// special literals, in addition to ordinary decimal notation (no hex/
// octal/binary floats). str must contain exactly this one value and
// nothing else; with len set, str need not be NUL-terminated.
static bool _sys_string_parse_double(const char *str, size_t len, double *out) {
  if (str == NULL) {
    return false;
  }
  const char *end = (len != 0) ? (str + len) : (str + sys_string_bytes(str));
  const char *pos = str;

  bool negative = false;
  if (pos < end && (*pos == '+' || *pos == '-')) {
    negative = (*pos == '-');
    pos++;
  }
  if (pos >= end) {
    return false; // nothing but a sign, or nothing at all
  }

  // NaN / Inf - checked against what's left of the input, not just a
  // fixed-length peek, so "NaN"/"Inf" plus trailing content still falls
  // through to the "nothing may follow" check below instead of matching
  // early and ignoring it.
  size_t remaining = (size_t)(end - pos);
  if (remaining == 3 && pos[0] == 'N' && pos[1] == 'a' && pos[2] == 'N') {
    if (out) {
      *out = negative ? -NAN : NAN;
    }
    return true;
  }
  if (remaining == 3 && pos[0] == 'I' && pos[1] == 'n' && pos[2] == 'f') {
    if (out) {
      *out = negative ? -INFINITY : INFINITY;
    }
    return true;
  }

  // Otherwise, an ordinary decimal literal. Either side of the '.' may
  // be empty - ".5" and "5." are both valid, matching plain C float
  // literal syntax - but not both at once ("." alone isn't a number).
  if (!((*pos >= '0' && *pos <= '9') || *pos == '.')) {
    return false;
  }

  double mantissa = 0.0;
  size_t nintdigits = 0;
  while (pos < end && *pos >= '0' && *pos <= '9') {
    mantissa = mantissa * 10.0 + (double)(*pos - '0');
    nintdigits++;
    pos++;
  }

  long exp10 = 0;
  size_t nfracdigits = 0;

  if (pos < end && *pos == '.') {
    pos++;
    while (pos < end && *pos >= '0' && *pos <= '9') {
      mantissa = mantissa * 10.0 + (double)(*pos - '0');
      if (exp10 > -_SYS_STRING_FLOAT_EXP_LIMIT) {
        exp10--;
      }
      nfracdigits++;
      pos++;
    }
  }

  if (nintdigits == 0 && nfracdigits == 0) {
    return false; // a lone '.', with no digit on either side of it
  }

  if (pos < end && (*pos == 'e' || *pos == 'E')) {
    pos++;
    bool exp_negative = false;
    if (pos < end && (*pos == '+' || *pos == '-')) {
      exp_negative = (*pos == '-');
      pos++;
    }
    long e = 0;
    size_t nexpdigits = 0;
    while (pos < end && *pos >= '0' && *pos <= '9') {
      if (e < _SYS_STRING_FLOAT_EXP_LIMIT) {
        e = e * 10 + (*pos - '0');
      }
      nexpdigits++;
      pos++;
    }
    if (nexpdigits == 0) {
      return false; // 'e'/'E' (and optional sign) with no digit after it
    }
    exp10 += exp_negative ? -e : e;
  }

  if (pos != end) {
    return false; // trailing content
  }

  if (exp10 > _SYS_STRING_FLOAT_EXP_LIMIT) {
    exp10 = _SYS_STRING_FLOAT_EXP_LIMIT;
  } else if (exp10 < -_SYS_STRING_FLOAT_EXP_LIMIT) {
    exp10 = -_SYS_STRING_FLOAT_EXP_LIMIT;
  }

  double result = mantissa * _sys_string_pow10(exp10);
  if (out) {
    *out = negative ? -result : result;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Parse a string as a 32-bit floating point number. Returns true on
 * success, false on failure.  */
bool sys_string_parse_float32(const char *str, size_t len, float *value) {
  double d;
  if (!_sys_string_parse_double(str, len, &d)) {
    return false;
  }
  if (value) {
    *value = (float)d;
  }
  return true;
}

/** @brief Parse a string as a 64-bit floating point number. Returns true on
 * success, false on failure.  */
bool sys_string_parse_float64(const char *str, size_t len, double *value) {
  double d;
  if (!_sys_string_parse_double(str, len, &d)) {
    return false;
  }
  if (value) {
    *value = d;
  }
  return true;
}
