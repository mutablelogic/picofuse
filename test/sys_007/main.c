#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

test_main_sys(0) {

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

  sys_sprintf(buf, sizeof(buf), "%lu", 5000000000ULL);
  test_assert_strequal(buf, "5000000000");

  // %zu tracks the platform's native size_t width (unlike %lu, which is
  // always 64-bit): on pico's 32-bit size_t, 5000000000 doesn't fit, so use
  // SIZE_MAX there instead to still exercise the full native width.
#if defined(SYSTEM_NAME_PICO)
  sys_sprintf(buf, sizeof(buf), "%zu", (size_t)SIZE_MAX);
  test_assert_strequal(buf, "4294967295");
#else
  sys_sprintf(buf, sizeof(buf), "%zu", (size_t)5000000000ULL);
  test_assert_strequal(buf, "5000000000");
#endif

  // Zero-pad + '#' prefix on the 64-bit path too.
  sys_sprintf(buf, sizeof(buf), "%#010lx", 0xABCDULL);
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

  sys_sprintf(buf, sizeof(buf), "%ld", -5000000000LL);
  test_assert_strequal(buf, "-5000000000");

  // %zd tracks the platform's native ptrdiff_t width (unlike %ld, which is
  // always 64-bit): on pico's 32-bit ptrdiff_t, -5000000000 doesn't fit, so
  // use INT32_MIN there instead to still exercise the full native width.
#if defined(SYSTEM_NAME_PICO)
  sys_sprintf(buf, sizeof(buf), "%zd", (ptrdiff_t)INT32_MIN);
  test_assert_strequal(buf, "-2147483648");
#else
  sys_sprintf(buf, sizeof(buf), "%zd", (ptrdiff_t)-5000000000LL);
  test_assert_strequal(buf, "-5000000000");
#endif

  // INT64_MIN: -num would overflow int64_t, exercising abs64's safe path.
  sys_sprintf(buf, sizeof(buf), "%lld", INT64_MIN);
  test_assert_strequal(buf, "-9223372036854775808");

  ///////////////////////////////////////////////////////////////////////////
  // %hu/%hhu/%hd/%hhd/%hx - 'h'/'hh' (short/char) length modifiers. A short
  // or char vararg is already promoted to int before it ever reaches
  // va_arg() (C's own default argument promotion), so these read exactly
  // like their unmodified %u/%d/%x counterparts - what matters here is
  // just that 'h'/'hh' get consumed as modifiers rather than misparsed as
  // the conversion specifier itself (see vprintf.c's own comment). This is
  // exactly what PRIu16/PRIx16/PRId16 (as lwIP's own debug builds format
  // addresses/ports with, via U16_F/X16_F/S16_F) expand to on this
  // toolchain.

  sys_sprintf(buf, sizeof(buf), "%hu", (unsigned short)42);
  test_assert_strequal(buf, "42");

  sys_sprintf(buf, sizeof(buf), "%hhu", (unsigned char)200);
  test_assert_strequal(buf, "200");

  sys_sprintf(buf, sizeof(buf), "%hd", (short)-42);
  test_assert_strequal(buf, "-42");

  sys_sprintf(buf, sizeof(buf), "%hhd", (signed char)-42);
  test_assert_strequal(buf, "-42");

  sys_sprintf(buf, sizeof(buf), "%hx", (unsigned short)255);
  test_assert_strequal(buf, "ff");

  // A format string with several %h-modified conversions in one call would
  // desync every argument after the first misparsed one if 'h' weren't
  // consumed correctly - matching lwIP's own
  // "%hu.%hu.%hu.%hu"-shaped IP address debug print.
  sys_sprintf(buf, sizeof(buf), "%hu.%hu.%hu.%hu", (unsigned short)192,
              (unsigned short)168, (unsigned short)1, (unsigned short)1);
  test_assert_strequal(buf, "192.168.1.1");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf(buf, sizeof(buf), "%3d", 7);
  test_assert_strequal(buf, "  7");
  test_assert(n == 3);

  size_t printed = sys_printf("%d %u\n", -1, 1u);
  test_assert(printed == 5); // "-1 1\n"

}
