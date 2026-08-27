#include <math.h>
#include <picofuse/sys.h>
#include <test/test.h>

int main(void) {
  sys_init();

  char buf[64];

  ///////////////////////////////////////////////////////////////////////////
  // Special values (_sys_printf_format_special): nan/inf, sign, case.

  sys_sprintf(buf, sizeof(buf), "%f", NAN);
  test_assert_strequal(buf, "nan");

  sys_sprintf(buf, sizeof(buf), "%F", NAN);
  test_assert_strequal(buf, "NAN");

  sys_sprintf(buf, sizeof(buf), "%f", INFINITY);
  test_assert_strequal(buf, "inf");

  sys_sprintf(buf, sizeof(buf), "%f", -INFINITY);
  test_assert_strequal(buf, "-inf");

  sys_sprintf(buf, sizeof(buf), "%E", -INFINITY);
  test_assert_strequal(buf, "-INF");

  ///////////////////////////////////////////////////////////////////////////
  // %f/%F - fixed-point (_sys_printf_format_fixed)

  sys_sprintf(buf, sizeof(buf), "%f", 0.0);
  test_assert_strequal(buf, "0.000000");

  sys_sprintf(buf, sizeof(buf), "%f", -0.0);
  test_assert_strequal(buf, "-0.000000");

  sys_sprintf(buf, sizeof(buf), "%f", 3.14159265358979);
  test_assert_strequal(buf, "3.141593"); // default precision 6

  sys_sprintf(buf, sizeof(buf), "%.2f", 3.14159265358979);
  test_assert_strequal(buf, "3.14");

  sys_sprintf(buf, sizeof(buf), "%.0f", 3.14159265358979);
  test_assert_strequal(buf, "3"); // no '.', no PREFIX flag

  sys_sprintf(buf, sizeof(buf), "%f", -100.0);
  test_assert_strequal(buf, "-100.000000");

  // Precision is clamped to SYS_PRINTF_MAX_FLOAT_PRECISION (9), not the 10
  // asked for here.
  sys_sprintf(buf, sizeof(buf), "%.10f", 3.14159265358979);
  test_assert_strequal(buf, "3.141592654");

  // Width + zero-pad + precision together.
  sys_sprintf(buf, sizeof(buf), "%010.2f", 3.14);
  test_assert_strequal(buf, "0000003.14");

  // Width + left-align: padding is always spaces, never zeros.
  sys_sprintf(buf, sizeof(buf), "%-10.2f", 3.14);
  test_assert_strequal(buf, "3.14      ");

  // Magnitude beyond UINT64_MAX falls back to exponential notation rather
  // than an unbounded fixed-point buffer.
  sys_sprintf(buf, sizeof(buf), "%f", 1e100);
  test_assert_strequal(buf, "1.000000e+100");

  ///////////////////////////////////////////////////////////////////////////
  // %e/%E - exponential (_sys_printf_format_exponential)

  sys_sprintf(buf, sizeof(buf), "%e", 3.14159265358979);
  test_assert_strequal(buf, "3.141593e+00");

  sys_sprintf(buf, sizeof(buf), "%.3e", 3.14159265358979);
  test_assert_strequal(buf, "3.142e+00"); // rounds up

  sys_sprintf(buf, sizeof(buf), "%e", 100.0);
  test_assert_strequal(buf, "1.000000e+02");

  sys_sprintf(buf, sizeof(buf), "%e", 1e-10);
  test_assert_strequal(buf, "1.000000e-10"); // 2-digit exponent minimum

  sys_sprintf(buf, sizeof(buf), "%e", 1e100);
  test_assert_strequal(buf, "1.000000e+100"); // 3-digit exponent, unpadded

  // Mantissa rounding carrying into the exponent (9.9999995 -> 1.0e+01).
  sys_sprintf(buf, sizeof(buf), "%.6e", 9.9999995);
  test_assert_strequal(buf, "1.000000e+01");

  ///////////////////////////////////////////////////////////////////////////
  // %g/%G - adaptive (_sys_printf_putf's mode selection + precision(adaptive))

  sys_sprintf(buf, sizeof(buf), "%g", 3.14159265358979);
  test_assert_strequal(buf, "3.14159"); // fixed, trailing zeros trimmed

  sys_sprintf(buf, sizeof(buf), "%g", 9999999.0);
  test_assert_strequal(buf, "1e+07"); // exponent(6) >= significant digits(6)

  sys_sprintf(buf, sizeof(buf), "%g", 0.999999);
  test_assert_strequal(buf, "0.999999");

  sys_sprintf(buf, sizeof(buf), "%g", 123.456);
  test_assert_strequal(buf, "123.456");

  sys_sprintf(buf, sizeof(buf), "%g", 1e100);
  test_assert_strequal(buf, "1e+100");

  sys_sprintf(buf, sizeof(buf), "%g", 1e-100);
  test_assert_strequal(buf, "1e-100");

  // Precision 0 on %g is adaptive-treated as 1, not 0.
  sys_sprintf(buf, sizeof(buf), "%.0g", 5.0);
  test_assert_strequal(buf, "5");

  // Explicit precision below the value's own significant-digit count,
  // pushed into the exponential branch (exponent(2) not < precision(2)).
  sys_sprintf(buf, sizeof(buf), "%.2g", 123.456);
  test_assert_strequal(buf, "1.2e+02");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf(buf, sizeof(buf), "%.1f", 2.5);
  test_assert_strequal(buf, "2.5");
  test_assert(n == 3);

  size_t printed = sys_printf("%.1f %g\n", 1.5, 2.0);
  test_assert(printed == 6); // "1.5 2\n"

  sys_exit();
  return 0;
}
