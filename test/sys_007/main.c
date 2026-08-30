#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  char buf[64];

  ///////////////////////////////////////////////////////////////////////////
  // %u/%x/%X/%b/%o - unsigned, 32-bit path (_sys_printf_putuv)

  sys_sprintf(buf, sizeof(buf), "%u", 42u);
  test_assert_strequal(buf, "42");

  sys_sprintf(buf, sizeof(buf), "%x", 255u);
  test_assert_strequal(buf, "ff");

  sys_sprintf(buf, sizeof(buf), "%X", 255u);
  test_assert_strequal(buf, "FF");

  sys_sprintf(buf, sizeof(buf), "%b", 5u);
  test_assert_strequal(buf, "101");

  sys_sprintf(buf, sizeof(buf), "%o", 8u);
  test_assert_strequal(buf, "10");

  // '#' prefix per base.
  sys_sprintf(buf, sizeof(buf), "%#x", 255u);
  test_assert_strequal(buf, "0xff");

  sys_sprintf(buf, sizeof(buf), "%#b", 5u);
  test_assert_strequal(buf, "0b101");

  sys_sprintf(buf, sizeof(buf), "%#o", 8u);
  test_assert_strequal(buf, "010");

  // Zero-pad vs. left-align.
  sys_sprintf(buf, sizeof(buf), "%05u", 42u);
  test_assert_strequal(buf, "00042");

  sys_sprintf(buf, sizeof(buf), "%-5u", 42u);
  test_assert_strequal(buf, "42   ");

  // Zero-pad + '#' prefix together: prefix counted in the pad-width math.
  sys_sprintf(buf, sizeof(buf), "%#06x", 5u);
  test_assert_strequal(buf, "0x0005");

  ///////////////////////////////////////////////////////////////////////////
  // %lu/%zu - unsigned, 64-bit path (_sys_printf_putuv64), values that only
  // fit in 64 bits to prove the wide path was actually taken.

  sys_sprintf(buf, sizeof(buf), "%lu", (unsigned long)5000000000ULL);
  test_assert_strequal(buf, "5000000000");

  sys_sprintf(buf, sizeof(buf), "%zu", (size_t)5000000000ULL);
  test_assert_strequal(buf, "5000000000");

  // Zero-pad + '#' prefix on the 64-bit path too.
  sys_sprintf(buf, sizeof(buf), "%#010lx", (unsigned long)0xABCDUL);
  test_assert_strequal(buf, "0x0000abcd");

  ///////////////////////////////////////////////////////////////////////////
  // %d - signed, 32-bit path (_sys_printf_putuv + _sys_printf_abs32)

  sys_sprintf(buf, sizeof(buf), "%d", 42);
  test_assert_strequal(buf, "42");

  sys_sprintf(buf, sizeof(buf), "%d", -42);
  test_assert_strequal(buf, "-42");

  sys_sprintf(buf, sizeof(buf), "%+d", 42);
  test_assert_strequal(buf, "+42");

  sys_sprintf(buf, sizeof(buf), "% d", 42);
  test_assert_strequal(buf, " 42");

  // Zero-pad + sign together.
  sys_sprintf(buf, sizeof(buf), "%05d", -42);
  test_assert_strequal(buf, "-0042");

  // INT32_MIN: -num would overflow int32_t, exercising abs32's safe path.
  sys_sprintf(buf, sizeof(buf), "%d", INT32_MIN);
  test_assert_strequal(buf, "-2147483648");

  ///////////////////////////////////////////////////////////////////////////
  // %ld/%zd - signed, 64-bit path (_sys_printf_putuv64 + _sys_printf_abs64)

  sys_sprintf(buf, sizeof(buf), "%ld", (long)-5000000000LL);
  test_assert_strequal(buf, "-5000000000");

  sys_sprintf(buf, sizeof(buf), "%zd", (ptrdiff_t)-5000000000LL);
  test_assert_strequal(buf, "-5000000000");

  // INT64_MIN: -num would overflow int64_t, exercising abs64's safe path.
  sys_sprintf(buf, sizeof(buf), "%lld", INT64_MIN);
  test_assert_strequal(buf, "-9223372036854775808");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf(buf, sizeof(buf), "%3d", 7);
  test_assert_strequal(buf, "  7");
  test_assert(n == 3);

  size_t printed = sys_printf("%d %u\n", -1, 1u);
  test_assert(printed == 5); // "-1 1\n"

  sys_exit();
  return 0;
}
