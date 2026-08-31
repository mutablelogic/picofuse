#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  char buf[64];

  // Plain %s: no width, no flags -> no padding at all.
  sys_sprintf(buf, sizeof(buf), "%s", "hello");
  test_assert_strequal(buf, "hello");

  // NULL argument substitutes the "<null>" placeholder.
  sys_sprintf(buf, sizeof(buf), "%s", (const char *)NULL);
  test_assert_strequal(buf, "<null>");

  // Empty string, no width: nothing to pad.
  sys_sprintf(buf, sizeof(buf), "%s", "");
  test_assert_strequal(buf, "");

  // Width smaller than the string's own length: no padding at all -
  // (state->width > len) is false.
  sys_sprintf(buf, sizeof(buf), "%3s", "hello");
  test_assert_strequal(buf, "hello");

  // Width larger than the string, right-aligned by default: left-padded.
  sys_sprintf(buf, sizeof(buf), "%8s", "hi");
  test_assert_strequal(buf, "      hi");

  // Width larger than the string, left-aligned via '-': right-padded.
  sys_sprintf(buf, sizeof(buf), "%-8s", "hi");
  test_assert_strequal(buf, "hi      ");

  // Width larger than an empty string: padding fills the whole width.
  sys_sprintf(buf, sizeof(buf), "%5s", "");
  test_assert_strequal(buf, "     ");

  // Multiple %s specifiers mixed with literal characters in one call.
  sys_sprintf(buf, sizeof(buf), "[%s-%s]", "a", "bc");
  test_assert_strequal(buf, "[a-bc]");

  // Return value matches the rendered length, through the sys_sprintf
  // buffer putch path (_sys_sprintf_putch).
  size_t n = sys_sprintf(buf, sizeof(buf), "%6s", "ok");
  test_assert_strequal(buf, "    ok");
  test_assert(n == 6);

  // Same logic through the console putch implementation
  // (_sys_printf_putch) instead of the buffer one.
  size_t printed = sys_printf("%s %s\n", "console", "path");
  test_assert(printed == 13); // "console path\n"

}
