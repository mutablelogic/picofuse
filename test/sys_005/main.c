#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  char buf[64];

  // Plain %c: no width, no flags -> no padding at all.
  sys_sprintf(buf, sizeof(buf), "%c", 'A');
  test_assert_strequal(buf, "A");

  // Width of exactly 1 takes the same "no padding" branch as width 0 -
  // (state->width > 1) is false either way.
  sys_sprintf(buf, sizeof(buf), "%1c", 'B');
  test_assert_strequal(buf, "B");

  // Width > 1, right-aligned by default: left-padded with spaces.
  sys_sprintf(buf, sizeof(buf), "%5c", 'C');
  test_assert_strequal(buf, "    C");

  // Width > 1, left-aligned via '-': right-padded with spaces.
  sys_sprintf(buf, sizeof(buf), "%-5c", 'D');
  test_assert_strequal(buf, "D    ");

  // Multiple %c specifiers mixed with literal characters in one call.
  sys_sprintf(buf, sizeof(buf), "[%c-%c]", 'X', 'Y');
  test_assert_strequal(buf, "[X-Y]");

  // Return value matches the actual rendered length, through the
  // sys_sprintf buffer putch path (_sys_sprintf_putch).
  size_t n = sys_sprintf(buf, sizeof(buf), "%3c", 'Z');
  test_assert_strequal(buf, "  Z");
  test_assert(n == 3);

  // Same code path through the console putch implementation
  // (_sys_printf_putch) instead of the buffer one, to exercise both.
  size_t printed = sys_printf("%c%c%c\n", 'a', 'b', 'c');
  test_assert(printed == 4); // 'a', 'b', 'c', '\n'

  sys_exit();
  return 0;
}
