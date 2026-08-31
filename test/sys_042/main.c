#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys() {

  ///////////////////////////////////////////////////////////////////////
  // sys_string_hasprefix

  test_assert(sys_string_hasprefix(NULL, NULL) == true);
  test_assert(sys_string_hasprefix(NULL, "") == true);
  test_assert(sys_string_hasprefix(NULL, "x") == false);
  test_assert(sys_string_hasprefix("hello", NULL) == true);
  test_assert(sys_string_hasprefix("hello", "") == true);
  test_assert(sys_string_hasprefix("hello", "he") == true);
  test_assert(sys_string_hasprefix("hello", "hello") == true);
  test_assert(sys_string_hasprefix("hello", "hello!") == false); // longer than s
  test_assert(sys_string_hasprefix("hello", "el") == false);     // not at the start
  test_assert(sys_string_hasprefix("Hello", "hello") == false);  // case-sensitive

  // Multi-byte prefix.
  test_assert(sys_string_hasprefix("caf\xC3\xA9lait", "caf") == true);
  test_assert(sys_string_hasprefix("caf\xC3\xA9lait", "caf\xC3\xA9") == true);

  ///////////////////////////////////////////////////////////////////////
  // sys_string_hassuffix

  test_assert(sys_string_hassuffix(NULL, NULL) == true);
  test_assert(sys_string_hassuffix(NULL, "") == true);
  test_assert(sys_string_hassuffix(NULL, "x") == false);
  test_assert(sys_string_hassuffix("hello", NULL) == true);
  test_assert(sys_string_hassuffix("hello", "") == true);
  test_assert(sys_string_hassuffix("hello", "lo") == true);
  test_assert(sys_string_hassuffix("hello", "hello") == true);
  test_assert(sys_string_hassuffix("hello", "!hello") == false); // longer than s
  test_assert(sys_string_hassuffix("hello", "el") == false);     // not at the end
  test_assert(sys_string_hassuffix("Hello", "hello") == false);  // case-sensitive

  // Multi-byte suffix.
  test_assert(sys_string_hassuffix("caf\xC3\xA9lait", "lait") == true);
  test_assert(sys_string_hassuffix("caf\xC3\xA9lait", "\xC3\xA9lait") == true);

}
