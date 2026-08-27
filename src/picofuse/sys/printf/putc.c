#include "types.h"
#include <stdarg.h> // for va_list, va_arg
#include <stddef.h> // for size_t

/** @brief Output a single character with optional padding based on the printf
 * state. */
size_t _sys_printf_putc(struct sys_printf_state *state, va_list *va) {
  int ch = va_arg(*va, int); // char is promoted to int in variadic functions
  size_t total_chars = 0;
  size_t padding = (state->width > 1) ? state->width - 1 : 0;

  // Right-aligned by default (left padding)
  if (!(state->flags & SYS_PRINTF_FLAG_LEFT)) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  // Output the character
  total_chars += state->putch(state, (char)ch);

  // Left-aligned (right padding)
  if (state->flags & SYS_PRINTF_FLAG_LEFT) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  return total_chars;
}
