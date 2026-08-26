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
 * "hw", "hid", "usb"), printed as "[context] " before the formatted
 * message. Pass NULL to omit the tag.
 *
 * Emits logs only when NDEBUG is not defined at compile time.
 */
#ifndef NDEBUG
/** @cond INTERNAL */
void _sys_debugf_impl(const char *context, const char *format, ...);
/** @endcond */
#define sys_debugf(...) _sys_debugf_impl(__VA_ARGS__)
#else
#define sys_debugf(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
