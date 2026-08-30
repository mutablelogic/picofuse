#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  char buf[64];

  ///////////////////////////////////////////////////////////////////////////
  // Zero value in each base.

  sys_sprintf(buf, sizeof(buf), "%x", 0u);
  test_assert_strequal(buf, "0");

  sys_sprintf(buf, sizeof(buf), "%o", 0u);
  test_assert_strequal(buf, "0");

  sys_sprintf(buf, sizeof(buf), "%b", 0u);
  test_assert_strequal(buf, "0");

  // Known deviation from standard printf: real printf suppresses the '#'
  // prefix for a zero value ("%#x" of 0 is just "0"); this implementation
  // does not special-case it and always adds the prefix when the flag is
  // set.
  sys_sprintf(buf, sizeof(buf), "%#x", 0u);
  test_assert_strequal(buf, "0x0");

  sys_sprintf(buf, sizeof(buf), "%#o", 0u);
  test_assert_strequal(buf, "00");

  sys_sprintf(buf, sizeof(buf), "%#b", 0u);
  test_assert_strequal(buf, "0b0");

  ///////////////////////////////////////////////////////////////////////////
  // Base-boundary digit counts (value == base -> "10").

  sys_sprintf(buf, sizeof(buf), "%x", 16u);
  test_assert_strequal(buf, "10");

  sys_sprintf(buf, sizeof(buf), "%o", 8u);
  test_assert_strequal(buf, "10");

  sys_sprintf(buf, sizeof(buf), "%b", 2u);
  test_assert_strequal(buf, "10");

  ///////////////////////////////////////////////////////////////////////////
  // Uppercase hex, with and without zero-padding.

  sys_sprintf(buf, sizeof(buf), "%X", 0xabcdu);
  test_assert_strequal(buf, "ABCD");

  sys_sprintf(buf, sizeof(buf), "%#X", 0xffu);
  test_assert_strequal(buf, "0XFF"); // uppercase prefix letter too

  sys_sprintf(buf, sizeof(buf), "%08X", 0xabcu);
  test_assert_strequal(buf, "00000ABC");

  ///////////////////////////////////////////////////////////////////////////
  // Space-padded width (no '0' flag) for each base.

  sys_sprintf(buf, sizeof(buf), "%6x", 0xffu);
  test_assert_strequal(buf, "    ff");

  sys_sprintf(buf, sizeof(buf), "%6o", 8u);
  test_assert_strequal(buf, "    10");

  sys_sprintf(buf, sizeof(buf), "%6b", 5u);
  test_assert_strequal(buf, "   101");

  ///////////////////////////////////////////////////////////////////////////
  // A negative int argument to %x/%o/%b is read as unsigned, so it's
  // reinterpreted as its raw bit pattern, not treated as an error.

  sys_sprintf(buf, sizeof(buf), "%x", -1);
  test_assert_strequal(buf, "ffffffff");

  sys_sprintf(buf, sizeof(buf), "%o", -1);
  test_assert_strequal(buf, "37777777777");

  sys_sprintf(buf, sizeof(buf), "%b", -2);
  test_assert_strequal(buf, "11111111111111111111111111111110");

  ///////////////////////////////////////////////////////////////////////////
  // 64-bit path (%lx/%lo/%lb) with a magnitude that genuinely exceeds 32
  // bits, proving it wasn't silently truncated.

  sys_sprintf(buf, sizeof(buf), "%lx", (unsigned long)0x100000000ULL);
  test_assert_strequal(buf, "100000000"); // 9 hex digits, not 8

  sys_sprintf(buf, sizeof(buf), "%lx", (unsigned long)UINT64_MAX);
  test_assert_strequal(buf, "ffffffffffffffff");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf(buf, sizeof(buf), "%#06x", 5u);
  test_assert_strequal(buf, "0x0005");
  test_assert(n == 6);

  size_t printed = sys_printf("%x %o %b\n", 255u, 8u, 5u);
  test_assert(printed == 10); // "ff 10 101\n"

  sys_exit();
  return 0;
}
