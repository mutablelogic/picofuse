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
