#include <picofuse/sys.h>
#include <stddef.h>
#include <test/test.h>

static const char *custom_handler(char spec, va_list *va) {
  if (spec == '@') {
    void *obj = va_arg(*va, void *);
    return (obj != NULL) ? "<obj>" : "<nil>";
  }
  return NULL; // Not handled by this custom handler.
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  char buf[64];

  // Handler recognizes '@' and its result is emitted verbatim.
  sys_sprintf_ex(buf, sizeof(buf), "%@", custom_handler, (void *)1);
  test_assert_strequal(buf, "<obj>");

  // Mixed with a built-in specifier and literal text in one call, proving
  // the va_list stays correctly aligned after the custom argument is
  // consumed.
  sys_sprintf_ex(buf, sizeof(buf), "[%@ n=%d]", custom_handler, (void *)1, 7);
  test_assert_strequal(buf, "[<obj> n=7]");

  // The handler itself decides what a NULL "object" renders as - the
  // printf machinery doesn't special-case it.
  sys_sprintf_ex(buf, sizeof(buf), "%@", custom_handler, (void *)0);
  test_assert_strequal(buf, "<nil>");

  // The handler declining a specifier (returns NULL) falls through to the
  // same "silently emit nothing" behavior as an unrecognized specifier
  // with no handler at all.
  sys_sprintf_ex(buf, sizeof(buf), "[%q]", custom_handler, (void *)1);
  test_assert_strequal(buf, "[]");

  // With no custom handler at all (plain sys_sprintf), an unrecognized
  // specifier is silently swallowed the same way.
  sys_sprintf(buf, sizeof(buf), "[%q]");
  test_assert_strequal(buf, "[]");

  // Known limitation: custom results are emitted raw with no width/padding
  // support - a width in the format string is silently ignored, unlike
  // every built-in specifier.
  sys_sprintf_ex(buf, sizeof(buf), "%10@", custom_handler, (void *)1);
  test_assert_strequal(buf, "<obj>");

  ///////////////////////////////////////////////////////////////////////////
  // Return value and the console putch path.

  size_t n = sys_sprintf_ex(buf, sizeof(buf), "%@", custom_handler, (void *)1);
  test_assert(n == 5); // strlen("<obj>")

  sys_exit();
  return 0;
}
