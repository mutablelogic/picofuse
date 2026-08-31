#include <picofuse/sys.h>
#include <math.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // Plain integers, signed and unsigned

  {
    float v = 0;
    test_assert(sys_string_parse_float32("123", 0, &v) == true);
    test_assert(v == 123.0f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("-123", 0, &v) == true);
    test_assert(v == -123.0f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("+123", 0, &v) == true);
    test_assert(v == 123.0f);
  }
  {
    float v = 1;
    test_assert(sys_string_parse_float32("0", 0, &v) == true);
    test_assert(v == 0.0f);
  }

  ///////////////////////////////////////////////////////////////////////
  // Fractional parts - values exactly representable in binary, so this
  // can assert exact equality rather than needing an epsilon.

  {
    float v = 0;
    test_assert(sys_string_parse_float32("0.5", 0, &v) == true);
    test_assert(v == 0.5f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("-0.25", 0, &v) == true);
    test_assert(v == -0.25f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("1.5", 0, &v) == true);
    test_assert(v == 1.5f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("0.125", 0, &v) == true);
    test_assert(v == 0.125f);
  }

  ///////////////////////////////////////////////////////////////////////
  // Exponents - again picking values that land on an exact result.

  {
    float v = 0;
    test_assert(sys_string_parse_float32("1e2", 0, &v) == true);
    test_assert(v == 100.0f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("5e-1", 0, &v) == true);
    test_assert(v == 0.5f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("1.25e2", 0, &v) == true);
    test_assert(v == 125.0f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("2E+3", 0, &v) == true); // uppercase E
    test_assert(v == 2000.0f);
  }

  ///////////////////////////////////////////////////////////////////////
  // NaN and Inf - exact spelling only

  {
    float v = 0;
    test_assert(sys_string_parse_float32("NaN", 0, &v) == true);
    test_assert(isnan(v));
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("Inf", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("+Inf", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("-Inf", 0, &v) == true);
    test_assert(isinf(v) && v < 0);
  }

  // Any other spelling is a parse error - case-sensitive, exact match
  // only, not accepted the way strtod's "inf"/"infinity" would be.
  {
    test_assert(sys_string_parse_float32("nan", 0, NULL) == false);
    test_assert(sys_string_parse_float32("NAN", 0, NULL) == false);
    test_assert(sys_string_parse_float32("inf", 0, NULL) == false);
    test_assert(sys_string_parse_float32("INF", 0, NULL) == false);
    test_assert(sys_string_parse_float32("Infinity", 0, NULL) == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // Overflow/underflow saturate to +-Inf/0 - not a parse error, since a
  // float can represent them directly.

  {
    float v = 0;
    test_assert(sys_string_parse_float32("1e400", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("-1e400", 0, &v) == true);
    test_assert(isinf(v) && v < 0);
  }
  {
    float v = 1;
    test_assert(sys_string_parse_float32("1e-400", 0, &v) == true);
    test_assert(v == 0.0f);
  }
  {
    // An absurdly large exponent must still resolve quickly (via
    // exponentiation by squaring, not one multiply per decimal place).
    float v = 0;
    test_assert(sys_string_parse_float32("1e999999999", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // float-specific: a magnitude well beyond FLT_MAX but comfortably
  // within a double's range still overflows to +Inf once narrowed.

  {
    float v = 0;
    test_assert(sys_string_parse_float32("1e100", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed - all parse errors

  {
    test_assert(sys_string_parse_float32(NULL, 0, NULL) == false);
    test_assert(sys_string_parse_float32("", 0, NULL) == false);
    test_assert(sys_string_parse_float32("+", 0, NULL) == false);
    test_assert(sys_string_parse_float32("-", 0, NULL) == false);
    test_assert(sys_string_parse_float32("abc", 0, NULL) == false);
    test_assert(sys_string_parse_float32(".", 0, NULL) == false); // nothing on either side
    test_assert(sys_string_parse_float32("+.", 0, NULL) == false);
    test_assert(sys_string_parse_float32("1e", 0, NULL) == false); // 'e' with nothing after
    test_assert(sys_string_parse_float32("1e+", 0, NULL) == false);
    test_assert(sys_string_parse_float32("1.2.3", 0, NULL) == false);
    test_assert(sys_string_parse_float32("0x1A", 0, NULL) == false); // no hex floats
    test_assert(sys_string_parse_float32("123abc", 0, NULL) == false);
    test_assert(sys_string_parse_float32("1 ", 0, NULL) == false);
  }

  // C-style leading/trailing dot - a digit on only one side of the '.'
  // is fine, matching plain C float literal syntax (".5f", "5.f"),
  // unlike JSON's stricter number grammar which requires both.
  {
    float v = 0;
    test_assert(sys_string_parse_float32(".5", 0, &v) == true);
    test_assert(v == 0.5f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("5.", 0, &v) == true);
    test_assert(v == 5.0f);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("-.25", 0, &v) == true);
    test_assert(v == -0.25f);
  }

  // *value is left unchanged on a parse error.
  {
    float v = 42.0f;
    test_assert(sys_string_parse_float32("not a number", 0, &v) == false);
    test_assert(v == 42.0f);
  }

  ///////////////////////////////////////////////////////////////////////
  // len == 0 vs len > 0 boundary enforcement, and NUL-termination not
  // required when len is given.

  {
    test_assert(sys_string_parse_float32("1.5abc", 0, NULL) == false);
  }
  {
    float v = 0;
    test_assert(sys_string_parse_float32("1.5abc", 3, &v) == true);
    test_assert(v == 1.5f);
  }
  {
    char buf[3] = {'2', '.', '5'}; // deliberately not NUL-terminated
    float v = 0;
    test_assert(sys_string_parse_float32(buf, 3, &v) == true);
    test_assert(v == 2.5f);
  }

}
