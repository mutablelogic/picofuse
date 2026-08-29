/**
 * @file sys/debugf.h
 * @brief Debug logging helpers for system output.
 * @ingroup SystemFormat
 */
#pragma once
#include "printf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Debug-only formatted logging helper.
 * @ingroup SystemFormat
 * @param context Short tag identifying the subsystem/module (for example
 * "hw", "hid", "usb"), printed as "[context] " after the "[DEBUG] " prefix
 * and before the formatted message. Pass NULL to omit the tag.
 * @param format A printf-style format string.
 * @param ... Additional arguments corresponding to format specifiers in
 * format.
 *
 * Emits logs only when NDEBUG is not defined at compile time.
 */
#ifndef NDEBUG
/** @cond INTERNAL */
void _sys_debugf_impl(const char *context, const char *format, ...);
/** @endcond */
#define sys_debugf(context, format, ...)                                     \
  _sys_debugf_impl(context, format, ##__VA_ARGS__)
#else
#define sys_debugf(context, format, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
