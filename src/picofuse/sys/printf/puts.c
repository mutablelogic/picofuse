#include "types.h"
#include <stdarg.h> // for va_list, va_arg
#include <stddef.h> // for size_t

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Placeholder for NULL strings
static const char *_nullstr = "<null>";

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Output a string with optional padding based on the printf state. */
size_t _sys_printf_puts(struct sys_printf_state *state, va_list *va) {
  const char *str = va_arg(*va, const char *);
  if (str == NULL) {
    str = _nullstr; // Use placeholder for NULL strings
  }

  size_t len = 0;
  const char *ptr = str;
  while (*ptr) {
    len++;
    ptr++;
  }

  size_t total_chars = 0;
  size_t padding = (state->width > len) ? state->width - len : 0;

  // Right-aligned by default (left padding)
  if (!(state->flags & SYS_PRINTF_FLAG_LEFT)) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  // Output the string
  while (*str) {
    total_chars += state->putch(state, *str++);
  }

  // Left-aligned (right padding)
  if (state->flags & SYS_PRINTF_FLAG_LEFT) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  return total_chars;
}
