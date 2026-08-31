#include <picofuse/sys.h>
#include <math.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // Same shared grammar as sys_string_parse_float32 - one form each, to
  // confirm it's wired through correctly.

  {
    double v = 0;
    test_assert(sys_string_parse_float64("123", 0, &v) == true);
    test_assert(v == 123.0);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("-0.25", 0, &v) == true);
    test_assert(v == -0.25);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("1.25e2", 0, &v) == true);
    test_assert(v == 125.0);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("2E+3", 0, &v) == true);
    test_assert(v == 2000.0);
  }
  {
    test_assert(sys_string_parse_float64("12.5abc", 0, NULL) == false);
    test_assert(sys_string_parse_float64(".", 0, NULL) == false);
    test_assert(sys_string_parse_float64("nan", 0, NULL) == false);
    test_assert(sys_string_parse_float64(NULL, 0, NULL) == false);
  }

  // C-style leading/trailing dot, matching sys_string_parse_float32.
  {
    double v = 0;
    test_assert(sys_string_parse_float64(".5", 0, &v) == true);
    test_assert(v == 0.5);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("5.", 0, &v) == true);
    test_assert(v == 5.0);
  }

  ///////////////////////////////////////////////////////////////////////
  // NaN and Inf

  {
    double v = 0;
    test_assert(sys_string_parse_float64("NaN", 0, &v) == true);
    test_assert(isnan(v));
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("Inf", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("+Inf", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("-Inf", 0, &v) == true);
    test_assert(isinf(v) && v < 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // A magnitude that overflows float32 but fits comfortably in a
  // double - the whole reason a 64-bit variant exists.

  {
    double v = 0;
    test_assert(sys_string_parse_float64("1e100", 0, &v) == true);
    test_assert(isfinite(v));
    test_assert(v > 1e99 && v < 1e101);
  }

  ///////////////////////////////////////////////////////////////////////
  // Overflow/underflow saturate to +-Inf/0, and an absurd exponent
  // still resolves quickly (exponentiation by squaring).

  {
    double v = 0;
    test_assert(sys_string_parse_float64("1e400", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("-1e400", 0, &v) == true);
    test_assert(isinf(v) && v < 0);
  }
  {
    double v = 1;
    test_assert(sys_string_parse_float64("1e-400", 0, &v) == true);
    test_assert(v == 0.0);
  }
  {
    double v = 0;
    test_assert(sys_string_parse_float64("1e999999999", 0, &v) == true);
    test_assert(isinf(v) && v > 0);
  }

  // *value is left unchanged on a parse error.
  {
    double v = -7.5;
    test_assert(sys_string_parse_float64("nope", 0, &v) == false);
    test_assert(v == -7.5);
  }

  ///////////////////////////////////////////////////////////////////////
  // len > 0, str need not be NUL-terminated.

  {
    double v = 0;
    test_assert(sys_string_parse_float64("1.5abc", 3, &v) == true);
    test_assert(v == 1.5);
  }
  {
    char buf[3] = {'2', '.', '5'}; // deliberately not NUL-terminated
    double v = 0;
    test_assert(sys_string_parse_float64(buf, 3, &v) == true);
    test_assert(v == 2.5);
  }

}
