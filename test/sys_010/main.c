#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

// %p zero-pads to the native pointer width, which is architecture-dependent:
// 8 hex digits on the 32-bit pico target, 16 on a 64-bit host.
#if defined(SYSTEM_NAME_PICO)
#define PTR_HEX_DIGITS 8
#define PTR_PAD_1234 "0x00001234"
#define PTR_PAD_ZERO "0x00000000"
#define PTR_PAD_DEADBEEF "0xdeadbeef"
#define PTR_PAD_AB "0x000000ab"
#define PTR_PAD_CD "0x000000cd"
#else
#define PTR_HEX_DIGITS 16
#define PTR_PAD_1234 "0x0000000000001234"
#define PTR_PAD_ZERO "0x0000000000000000"
#define PTR_PAD_DEADBEEF "0x00000000deadbeef"
#define PTR_PAD_AB "0x00000000000000ab"
#define PTR_PAD_CD "0x00000000000000cd"
#endif

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  char buf[64];

  sys_sprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x1234);
  test_assert_strequal(buf, PTR_PAD_1234);

  // NULL still renders the full zero-padded width, not "(nil)" or similar.
  sys_sprintf(buf, sizeof(buf), "%p", (void *)0);
  test_assert_strequal(buf, PTR_PAD_ZERO);

  // A width from the format string is silently ignored: %p always forces
  // its own native-pointer-width zero-padding, discarding any user width.
  sys_sprintf(buf, sizeof(buf), "%20p", (void *)(uintptr_t)0x1234);
  test_assert_strequal(buf, PTR_PAD_1234);

  // Likewise '-' (left-align) has no visible effect: the forced zero-pad
  // already consumes the entire field before alignment is ever applied.
  sys_sprintf(buf, sizeof(buf), "%-p", (void *)(uintptr_t)0x1234);
  test_assert_strequal(buf, PTR_PAD_1234);

  // Always lowercase hex digits, regardless of case-sensitive flags
  // elsewhere in printf (%p has no uppercase variant).
  sys_sprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0xDEADBEEFu);
  test_assert_strequal(buf, PTR_PAD_DEADBEEF);

  // Multiple %p specifiers mixed with literal text in one call.
  sys_sprintf(buf, sizeof(buf), "[%p-%p]", (void *)(uintptr_t)0xAB,
              (void *)(uintptr_t)0xCD);
  test_assert_strequal(buf, "[" PTR_PAD_AB "-" PTR_PAD_CD "]");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x1);
  test_assert(n == 2 + PTR_HEX_DIGITS); // "0x" + native hex digits

  size_t printed = sys_printf("%p\n", (void *)(uintptr_t)0x1);
  test_assert(printed == n + 1); // n + '\n'

  sys_exit();
  return 0;
}
