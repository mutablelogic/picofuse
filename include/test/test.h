/**
 * @file test/test.h
 * @brief Assertion macros for picofuse system tests.
 */
#pragma once
#include <picofuse/sys.h>
#include <string.h>

/**
 * @def test_assert(condition)
 * @brief Asserts that a condition is true, panicking with the failed
 * condition and file/line context if it is not. Always checked, regardless
 * of NDEBUG.
 */
#define test_assert(condition)                                                \
  do {                                                                        \
    if (!(condition)) {                                                      \
      sys_panicf("[TEST] FAIL: %s, file %s, line %d", #condition, __FILE__,  \
                 __LINE__);                                                   \
    }                                                                         \
  } while (0)

/**
 * @def test_assert_strequal(actual, expected)
 * @brief Asserts that two null-terminated strings are equal, panicking with
 * both values and file/line context if they are not.
 */
#define test_assert_strequal(actual, expected)                                \
  do {                                                                        \
    const char *_test_actual = (actual);                                    \
    const char *_test_expected = (expected);                                \
    if (strcmp(_test_actual, _test_expected) != 0) {                        \
      sys_panicf("[TEST] FAIL: expected \"%s\" but got \"%s\", file %s, "    \
                 "line %d",                                                   \
                 _test_expected, _test_actual, __FILE__, __LINE__);          \
    }                                                                         \
  } while (0)

/**
 * @def test_main_sys()
 * @brief Declares a test's entry point in place of a raw main(). Wraps
 * sys_init()/sys_exit() around the test body and brackets it with
 * "[TEST] [INIT] <env>" / "[TEST] [EXIT] <env>" markers, where <env> is
 * sys_env_name().
 *
 * Usage:
 *   test_main_sys() {
 *     ...test body...
 *   }
 */
#define test_main_sys()                                                      \
  static void _test_main(void);                                             \
  int main(int argc, char *argv[]) {                                         \
    sys_init(argc, argv);                                                    \
    sys_printf("[TEST] [INIT] %s\n", sys_env_name());                        \
    _test_main();                                                            \
    sys_printf("[TEST] [EXIT] %s\n", sys_env_name());                        \
    sys_exit();                                                              \
    return 0;                                                                \
  }                                                                          \
  static void _test_main(void)
