#include <picofuse/sys.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

#define _NS_SECOND 1000000000ULL

// Units, longest first where one is the start of another ("ms" before "m",
// "days" before "day" before "d")
static const struct {
  const char *name;
  uint64_t ns;
} _sys_string_duration_units[] = {
    {"days", 86400ULL * _NS_SECOND}, {"day", 86400ULL * _NS_SECOND},
    {"d", 86400ULL * _NS_SECOND},    {"h", 3600ULL * _NS_SECOND},
    {"ms", 1000000ULL},              {"m", 60ULL * _NS_SECOND},
    {"s", _NS_SECOND},               {"us", 1000ULL},
    {"\xC2\xB5s", 1000ULL},          {"ns", 1ULL},
};

static inline bool _sys_string_isdigit(char c) { return c >= '0' && c <= '9'; }

static inline const char *_sys_string_skip_spaces(const char *pos,
                                                  const char *end) {
  while (pos < end && *pos == ' ') {
    pos++;
  }
  return pos;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

bool sys_string_parse_duration(const char *str, size_t len, int64_t *ns) {
  if (str == NULL || ns == NULL) {
    return false;
  }
  const char *end = (len != 0) ? (str + len) : (str + sys_string_bytes(str));
  const char *pos = str;

  bool negative = false;
  if (pos < end && (*pos == '+' || *pos == '-')) {
    negative = (*pos == '-');
    pos++;
  }

  // The most it can be, as a magnitude
  uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
  uint64_t total = 0;
  bool any = false;
  while (pos < end || !any) {
    // Spaces between amounts, but not after the last
    if (any) {
      pos = _sys_string_skip_spaces(pos, end);
      if (pos == end) {
        return false;
      }
    }

    // The whole number, and the fraction, of which at most 18 digits count
    uint64_t whole = 0;
    bool digits = false;
    while (pos < end && _sys_string_isdigit(*pos)) {
      if (__builtin_mul_overflow(whole, 10, &whole) ||
          __builtin_add_overflow(whole, (uint64_t)(*pos - '0'), &whole)) {
        return false;
      }
      digits = true;
      pos++;
    }
    uint64_t fraction = 0;
    double scale = 1;
    if (pos < end && *pos == '.') {
      pos++;
      int count = 0;
      while (pos < end && _sys_string_isdigit(*pos)) {
        if (count < 18) {
          fraction = fraction * 10 + (uint64_t)(*pos - '0');
          scale *= 10;
          count++;
        }
        digits = true;
        pos++;
      }
    }
    if (!digits) {
      return false;
    }

    // The unit, which may have spaces before it
    pos = _sys_string_skip_spaces(pos, end);
    uint64_t unit = 0;
    for (size_t i = 0; i < sizeof(_sys_string_duration_units) /
                               sizeof(_sys_string_duration_units[0]);
         i++) {
      const char *name = _sys_string_duration_units[i].name;
      size_t n = sys_string_bytes(name);
      if ((size_t)(end - pos) >= n && __builtin_memcmp(pos, name, n) == 0 &&
          (pos + n == end || !(pos[n] >= 'a' && pos[n] <= 'z'))) {
        unit = _sys_string_duration_units[i].ns;
        pos += n;
        break;
      }
    }
    if (unit == 0) {
      return false;
    }

    // Add it, truncating any part of a nanosecond
    uint64_t value;
    if (__builtin_mul_overflow(whole, unit, &value)) {
      return false;
    }
    double part = (double)fraction * ((double)unit / scale);
    if (part >= (double)UINT64_MAX ||
        __builtin_add_overflow(value, (uint64_t)part, &value) ||
        __builtin_add_overflow(total, value, &total) || total > limit) {
      return false;
    }
    any = true;
  }

  *ns = negative ? (int64_t)(0 - total) : (int64_t)total;
  return true;
}
