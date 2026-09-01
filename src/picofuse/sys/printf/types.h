#pragma once
#include <picofuse/sys/io.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef enum {
  SYS_PRINTF_FLAG_SIZET = 1 << 0,  /**< Flag for size_t specifier */
  SYS_PRINTF_FLAG_LONG = 1 << 1,   /**< Flag for long integer specifier */
  SYS_PRINTF_FLAG_LEFT = 1 << 2,   /**< Flag for left alignment */
  SYS_PRINTF_FLAG_SIGN = 1 << 3,   /**< Flag to force numeric sign */
  SYS_PRINTF_FLAG_PREFIX = 1 << 4, /**< Flag to force 0, 0x or 0b prefix */
  SYS_PRINTF_FLAG_PAD = 1 << 5,    /**< Flag for zero-padding */
  SYS_PRINTF_FLAG_NEG = 1 << 6,    /**< Flag to process as negative number */
  SYS_PRINTF_FLAG_HEX = 1 << 7,    /**< Flag for hexadecimal output */
  SYS_PRINTF_FLAG_BIN = 1 << 8,    /**< Flag for binary output */
  SYS_PRINTF_FLAG_OCT = 1 << 9,    /**< Flag for octal output */
  SYS_PRINTF_FLAG_UPPER = 1 << 10, /**< Flag for uppercase output */
  SYS_PRINTF_FLAG_SPACE =
      1 << 11, /**< Flag to prefix positive values with a space */
  SYS_PRINTF_FLAG_PRECISION = 1 << 12 /**< Flag for explicit precision */
} sys_printf_flags_t;

struct sys_printf_state {
  char *buffer;           /**< Buffer for formatted output */
  sys_iostream_t *stream; /**< Stream for formatted output */
  size_t size;            /**< Size of the buffer, including null terminator */
  size_t pos;             /**< Current position in the buffer */
  size_t (*putch)(struct sys_printf_state *state,
                  char ch); /**< Function to output a character */
  const char *(*custom)(char format, va_list *va); /**< Custom format handler */
  size_t width;             /**< Width specifier for padding */
  size_t precision;         /**< Precision specifier for formatting */
  sys_printf_flags_t flags; /**< Current format flags */
};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS
//
// Each specifier handler lives in its own translation unit (putc.c, puts.c,
// putd.c, ...); these declarations are what let put.c's dispatcher, and each
// other, call across those files.

/** @brief Handles the %c specifier. */
size_t _sys_printf_putc(struct sys_printf_state *state, va_list *va);

/** @brief Handles the %s specifier. */
size_t _sys_printf_puts(struct sys_printf_state *state, va_list *va);

/** @brief Renders a 32-bit unsigned magnitude. Callers must not pass a value
 * that may exceed 32 bits — use _sys_printf_putuv64() for that. */
size_t _sys_printf_putuv(struct sys_printf_state *state, uint32_t num);

/** @brief Renders a 64-bit unsigned magnitude. */
size_t _sys_printf_putuv64(struct sys_printf_state *state, uint64_t num);

/** @brief Handles the %u/%x/%X/%b/%o specifiers. */
size_t _sys_printf_putu(struct sys_printf_state *state, va_list *va);

/** @brief Handles the %d specifier. */
size_t _sys_printf_putd(struct sys_printf_state *state, va_list *va);

/** @brief Handles the %f/%F/%e/%E/%g/%G specifiers. */
size_t _sys_printf_putf(struct sys_printf_state *state, char spec, va_list *va);

/** @brief Dispatches a single format specifier to the handler above. */
size_t _sys_printf_put(struct sys_printf_state *state, char spec, va_list *va);

/** @brief Core formatted-output loop, shared by every public entry point.
 * Does not touch the printf mutex — callers that need atomicity across
 * multiple output calls (e.g. _sys_debugf_impl's tag plus message) must
 * hold it themselves around their whole sequence. */
size_t _sys_vprintf(struct sys_printf_state *state, const char *format,
                    va_list *va);

/** @brief putch callback that writes a character to the console via
 * sys_putch(). */
size_t _sys_printf_putch(struct sys_printf_state *state, char ch);

/** @brief putch callback that writes a character into a caller-supplied
 * buffer (struct sys_printf_state::buffer / ::size). */
size_t _sys_sprintf_putch(struct sys_printf_state *state, char ch);

/** @brief putch callback that writes a character to a stream. */
size_t _sys_fprintf_putch(struct sys_printf_state *state, char ch);
