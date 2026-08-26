/**
 * @file sys/assert.h
 * @brief Defines a custom assertion macro.
 * @ingroup System
 * This file provides an `sys_assert` macro that can be used for debugging
 * purposes.
 */

#pragma once

#include "panicf.h"

/**
 * @def sys_assert(condition)
 * @ingroup System
 * @brief Asserts that a condition is true.
 * @param condition The condition to check.
 * @details If the condition is false, it will call `panicf` with an assertion
 * failure message, including the condition, file name, and line number.
 */
#ifndef NDEBUG
#define sys_assert(condition)                                                 \
  do {                                                                        \
    if (!(condition)) {                                                      \
      sys_panicf("[ASSERT] FAIL: %s, file %s, line %d", #condition, __FILE__,\
                 __LINE__);                                                  \
    }                                                                        \
  } while (0)
#else
#define sys_assert(condition)                                                 \
  do {                                                                        \
    if (!(condition)) {                                                      \
      sys_panicf("[ASSERT] FAIL: %s", #condition);                           \
    }                                                                        \
  } while (0)
#endif
