#include <picofuse/sys.h>
#include <test/test.h>

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // sys_string_trimprefix

  test_assert(sys_string_trimprefix(NULL, NULL) == NULL);
  test_assert(sys_string_trimprefix(NULL, "x") == NULL);

  {
    char buf[] = "hello";
    test_assert(sys_string_trimprefix(buf, NULL) == buf); // no-op, same pointer
    test_assert_strequal(buf, "hello");
  }
  {
    char buf[] = "hello";
    test_assert(sys_string_trimprefix(buf, "") == buf);
    test_assert_strequal(buf, "hello");
  }
  {
    char buf[] = "hello";
    char *ret = sys_string_trimprefix(buf, "he");
    test_assert(ret == buf); // same pointer - remainder shifted down, not advanced
    test_assert_strequal(ret, "llo");
  }
  {
    char buf[] = "hello";
    test_assert(sys_string_trimprefix(buf, "xy") == buf); // not a prefix: unchanged
    test_assert_strequal(buf, "hello");
  }
  {
    char buf[] = "hello";
    char *ret = sys_string_trimprefix(buf, "hello");
    test_assert(ret == buf);
    test_assert_strequal(ret, ""); // whole string removed
  }
  {
    char buf[] = "hello";
    test_assert(sys_string_trimprefix(buf, "hello!") == buf); // longer than s
    test_assert_strequal(buf, "hello");
  }
  {
    // Multi-byte prefix.
    char buf[] = "caf\xC3\xA9lait";
    char *ret = sys_string_trimprefix(buf, "caf\xC3\xA9");
    test_assert(ret == buf);
    test_assert_strequal(ret, "lait");
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_string_trimsuffix

  test_assert(sys_string_trimsuffix(NULL, NULL) == NULL);
  test_assert(sys_string_trimsuffix(NULL, "x") == NULL);

  {
    char buf[] = "hello";
    test_assert(sys_string_trimsuffix(buf, NULL) == buf);
    test_assert_strequal(buf, "hello");
  }
  {
    char buf[] = "hello";
    test_assert(sys_string_trimsuffix(buf, "") == buf);
    test_assert_strequal(buf, "hello");
  }
  {
    char buf[] = "hello";
    char *ret = sys_string_trimsuffix(buf, "lo");
    test_assert(ret == buf); // start pointer never moves
    test_assert_strequal(ret, "hel");
  }
  {
    char buf[] = "hello";
    test_assert(sys_string_trimsuffix(buf, "xy") == buf); // not a suffix: unchanged
    test_assert_strequal(buf, "hello");
  }
  {
    char buf[] = "hello";
    char *ret = sys_string_trimsuffix(buf, "hello");
    test_assert_strequal(ret, ""); // whole string removed
  }
  {
    char buf[] = "hello";
    test_assert(sys_string_trimsuffix(buf, "!hello") == buf); // longer than s
    test_assert_strequal(buf, "hello");
  }
  {
    // Multi-byte suffix.
    char buf[] = "caf\xC3\xA9lait";
    char *ret = sys_string_trimsuffix(buf, "\xC3\xA9lait");
    test_assert_strequal(ret, "caf");
  }

  sys_exit();
  return 0;
}
