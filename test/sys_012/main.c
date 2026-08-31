#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  char buf[64];

  // A bare "%%" renders as a single literal '%'.
  sys_sprintf(buf, sizeof(buf), "%%");
  test_assert_strequal(buf, "%");

  // "%%" mixed with literal text.
  sys_sprintf(buf, sizeof(buf), "100%% done");
  test_assert_strequal(buf, "100% done");

  // "%%" alongside a real specifier in the same call.
  sys_sprintf(buf, sizeof(buf), "%d%%", 5);
  test_assert_strequal(buf, "5%");

  // Two "%%" in a row.
  sys_sprintf(buf, sizeof(buf), "%%%%");
  test_assert_strequal(buf, "%%");

  // A '%' with no specifier after it (end of string) is emitted literally
  // rather than being an error or being swallowed.
  sys_sprintf(buf, sizeof(buf), "trailing%");
  test_assert_strequal(buf, "trailing%");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf(buf, sizeof(buf), "%%");
  test_assert(n == 1);

  size_t printed = sys_printf("%d%%\n", 50);
  test_assert(printed == 4); // "50%\n"

}
