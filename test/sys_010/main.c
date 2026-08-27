#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

int main(void) {
  sys_init();

  char buf[64];

  sys_sprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x1234);
  test_assert_strequal(buf, "0x0000000000001234");

  // NULL still renders the full zero-padded width, not "(nil)" or similar.
  sys_sprintf(buf, sizeof(buf), "%p", (void *)0);
  test_assert_strequal(buf, "0x0000000000000000");

  // A width from the format string is silently ignored: %p always forces
  // its own native-pointer-width zero-padding, discarding any user width.
  sys_sprintf(buf, sizeof(buf), "%20p", (void *)(uintptr_t)0x1234);
  test_assert_strequal(buf, "0x0000000000001234");

  // Likewise '-' (left-align) has no visible effect: the forced zero-pad
  // already consumes the entire field before alignment is ever applied.
  sys_sprintf(buf, sizeof(buf), "%-p", (void *)(uintptr_t)0x1234);
  test_assert_strequal(buf, "0x0000000000001234");

  // Always lowercase hex digits, regardless of case-sensitive flags
  // elsewhere in printf (%p has no uppercase variant).
  sys_sprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0xDEADBEEFu);
  test_assert_strequal(buf, "0x00000000deadbeef");

  // Multiple %p specifiers mixed with literal text in one call.
  sys_sprintf(buf, sizeof(buf), "[%p-%p]", (void *)(uintptr_t)0xAB,
              (void *)(uintptr_t)0xCD);
  test_assert_strequal(buf, "[0x00000000000000ab-0x00000000000000cd]");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x1);
  test_assert(n == 18); // "0x" + 16 hex digits

  size_t printed = sys_printf("%p\n", (void *)(uintptr_t)0x1);
  test_assert(printed == 19); // 18 + '\n'

  sys_exit();
  return 0;
}
