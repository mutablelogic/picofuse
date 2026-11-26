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

    // The whole number, and the digits of the fraction, of which those past
    // the 32nd are too small to count
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
    uint8_t fraction[32];
    size_t count = 0;
    if (pos < end && *pos == '.') {
      pos++;
      while (pos < end && _sys_string_isdigit(*pos)) {
        if (count < sizeof(fraction)) {
          fraction[count++] = (uint8_t)(*pos - '0');
        }
        digits = true;
        pos++;
      }
    }
    if (!digits) {
      return false;
    }

    // The unit
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

    // The fraction of the unit, truncated to a whole nanosecond, from the
    // last digit to the first: dividing by ten at each step, and dropping
    // the remainder, gives the same as dropping it once at the end. Each step
    // is less than ten units, so it can't overflow.
    uint64_t part = 0;
    while (count > 0) {
      part = (fraction[--count] * unit + part) / 10;
    }

    // Add it
    uint64_t value;
    if (__builtin_mul_overflow(whole, unit, &value) ||
        __builtin_add_overflow(value, part, &value) ||
        __builtin_add_overflow(total, value, &total) || total > limit) {
      return false;
    }
    any = true;
  }

  *ns = negative ? (int64_t)(0 - total) : (int64_t)total;
  return true;
}
