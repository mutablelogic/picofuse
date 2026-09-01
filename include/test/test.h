/**
 * @file test/test.h
 * @brief Assertion macros for picofuse system tests.
 */
#pragma once
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>

/**
 * @def test_assert(condition)
 * @brief Asserts that a condition is true, panicking with the failed
 * condition and file/line context if it is not. Always checked, regardless
 * of NDEBUG.
 */
#define test_assert(condition)                                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      sys_panicf("[TEST] FAIL: %s, file %s, line %d", #condition, __FILE__,    \
                 __LINE__);                                                    \
    }                                                                          \
  } while (0)

/**
 * @def test_assert_strequal(actual, expected)
 * @brief Asserts that two null-terminated strings are equal, panicking with
 * both values and file/line context if they are not.
 */
#define test_assert_strequal(actual, expected)                                 \
  do {                                                                         \
    const char *_test_actual = (actual);                                       \
    const char *_test_expected = (expected);                                   \
    if (strcmp(_test_actual, _test_expected) != 0) {                           \
      sys_panicf("[TEST] FAIL: expected \"%s\" but got \"%s\", file %s, "      \
                 "line %d",                                                    \
                 _test_expected, _test_actual, __FILE__, __LINE__);            \
    }                                                                          \
  } while (0)

/**
 * @def test_main_sys(arena_size)
 * @brief Declares a test's entry point in place of a raw main(). Wraps
 * sys_init()/sys_exit() around the test body and brackets it with
 * "[TEST] [INIT] \<env\>" / "[TEST] [EXIT] \<env\>" markers, where `\<env\>`
 * is sys_env_name(). The test body receives the process's own (argc, argv) -
 * unused by most tests, so they're marked maybe-unused to stay warning-free.
 * @param arena_size Forwarded to sys_init() - the default arena's capacity
 * in bytes, or `0` to leave sys_malloc() and friends routed to the system
 * allocator.
 *
 * Usage:
 *   test_main_sys(0) {
 *     ...test body, optionally using argc/argv...
 *   }
 */
#define test_main_sys(arena_size)                                              \
  static void _test_main(int argc, char *argv[]);                              \
  int main(int argc, char *argv[]) {                                           \
    sys_init(argc, argv, (arena_size), sys_stdio_rtt);                         \
    sys_printf("[TEST] [INIT] %s\n", sys_env_name());                          \
    _test_main(argc, argv);                                                    \
    sys_printf("[TEST] [EXIT] %s\n", sys_env_name());                          \
    sys_exit();                                                                \
    return 0;                                                                  \
  }                                                                            \
  static void _test_main(int argc __attribute__((unused)),                     \
                         char *argv[] __attribute__((unused)))

/**
 * @def test_main_hw(arena_size)
 * @brief Like test_main_sys(), but also wraps hw_init()/hw_exit() around the
 * test body, inside the sys_init()/sys_exit() pair (hw depends on sys, so it
 * must be initialized after and torn down before it).
 * @param arena_size Forwarded to sys_init() - the default arena's capacity
 * in bytes, or `0` to leave sys_malloc() and friends routed to the system
 * allocator.
 *
 * Usage:
 *   test_main_hw(0) {
 *     ...test body, optionally using argc/argv...
 *   }
 */
#define test_main_hw(arena_size)                                               \
  static void _test_main(int argc, char *argv[]);                              \
  int main(int argc, char *argv[]) {                                           \
    sys_init(argc, argv, (arena_size), sys_stdio_rtt);                         \
    sys_printf("[TEST] [INIT] %s\n", sys_env_name());                          \
    hw_init();                                                                 \
    _test_main(argc, argv);                                                    \
    hw_exit();                                                                 \
    sys_printf("[TEST] [EXIT] %s\n", sys_env_name());                          \
    sys_exit();                                                                \
    return 0;                                                                  \
  }                                                                            \
  static void _test_main(int argc __attribute__((unused)),                     \
                         char *argv[] __attribute__((unused)))
