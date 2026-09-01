/**
 * @file stdio.h
 * @brief Standard input and output streams.
 */
#pragma once
#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Standard input/output backend types.
 * @ingroup System
 * @headerfile stdio.h picofuse/sys.h
 */
typedef enum sys_stdio_t {
  /** @brief No standard input/output backend. */
  sys_stdio_none = 0,
  /** @brief UART standard input/output backend. */
  sys_stdio_uart,
  /** @brief USB standard input/output backend. */
  sys_stdio_usb,
  /** @brief SEGGER RTT standard input/output backend. */
  sys_stdio_rtt,
} sys_stdio_t;

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

/**
 * @brief Standard output stream.
 * @ingroup System
 *
 * Set by sys_init(), or NULL if no output stream is available.
 */
extern sys_iostream_t *sys_stdout;

/**
 * @brief Standard input stream.
 * @ingroup System
 *
 * Set by sys_init(), or NULL if no input stream is available.
 */
extern sys_iostream_t *sys_stdin;

#ifdef __cplusplus
}
#endif