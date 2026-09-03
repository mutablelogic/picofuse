#include "mutex.h"
#include "types.h"
#include <picofuse/sys.h> // for sys_putch
#include <stdarg.h>       // for va_list, va_arg
#include <stddef.h>       // for size_t

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Perform formatted output to a custom printf state using a va_list.
 *
 * @param state The printf state containing flags, width, precision, and output
 * functions.
 * @param format The format string specifying how to format the output.
 * @param va A pointer to the va_list containing the variable arguments.
 * @return The total number of characters written to the output.
 */
size_t _sys_vprintf(struct sys_printf_state *state, const char *format,
                    va_list *va) {

  state->pos = 0; // Set the length
  while (*format) {
    char ch = *format++;

    // Handle non-formatting
    if (ch != '%') {
      state->pos += state->putch(state, ch);
      continue;
    }

    // Check for %% (escaped percent)
    if (*format == '%') {
      state->pos += state->putch(state, '%');
      format++; // Skip the second %
      continue;
    }

    // Parse format specifier with flags
    state->flags = 0;     // Reset flags for each format specifier
    state->width = 0;     // Reset width for each format specifier
    state->precision = 0; // Reset precision for each format specifier

    // Parse flags
    while (*format) {
      switch (*format) {
      case '-':
        state->flags |= SYS_PRINTF_FLAG_LEFT;
        state->flags &= ~SYS_PRINTF_FLAG_PAD;
        format++;
        break;
      case '+':
        state->flags |= SYS_PRINTF_FLAG_SIGN;
        format++;
        break;
      case ' ':
        state->flags |= SYS_PRINTF_FLAG_SPACE;
        format++;
        break;
      case '0':
        if (!(state->flags & SYS_PRINTF_FLAG_LEFT)) {
          state->flags |= SYS_PRINTF_FLAG_PAD;
        }
        format++;
        break;
      case '#':
        state->flags |= SYS_PRINTF_FLAG_PREFIX;
        format++;
        break;
      default:
        goto parse_width; // Exit flag parsing loop
      }
    }

  parse_width:
    // Parse width specifier (numeric digits)
    while (*format >= '0' && *format <= '9') {
      state->width = state->width * 10 + (*format - '0');
      format++;
    }

    if (*format == '.') {
      state->flags |= SYS_PRINTF_FLAG_PRECISION;
      format++;

      while (*format >= '0' && *format <= '9') {
        state->precision = state->precision * 10 + (*format - '0');
        format++;
      }
    }

    while (*format) {
      switch (*format) {
      case 'l':
        state->flags |= SYS_PRINTF_FLAG_LONG;
        format++;
        break;
      case 'z':
        state->flags |= SYS_PRINTF_FLAG_SIZET;
        format++;
        break;
      default:
        goto handle_specifier;
      }
    }

  handle_specifier:
    if (*format) {
      char spec = *format++;
      state->pos += _sys_printf_put(state, spec, va);
    } else {
      // If we reach here, it means we had a '%' at the end without a
      // specifier
      state->pos += state->putch(state, '%');
    }
  }

  return state->pos;
}

/** @brief Default character output function for sys_printf.
 *
 * @param state The current printf state.
 * @param ch The character to output.
 * @return The number of characters written (always 1 in this implementation).
 */
size_t _sys_printf_putch(struct sys_printf_state *state, char ch) {
  (void)state; // Unused in this implementation
  sys_putch(ch);
  return 1;
}

/** @brief Default character output function for sys_vsprintf.
 *
 * @param state The current printf state.
 * @param ch The character to output.
 * @return The number of characters written (always 0 in this implementation).
 */
size_t _sys_sprintf_putch(struct sys_printf_state *state, char ch) {
  // Write character at current position and increment immediately
  if (state->buffer && state->size > 0 && state->pos < state->size - 1) {
    state->buffer[state->pos] = ch;
  }
  state->pos++; // Increment position immediately after writing
  return 0;     // Return 0 so main loop doesn't double-increment
}

size_t _sys_fprintf_putch(struct sys_printf_state *state, char ch) {
  // sys_iostream_write() documents a short (including zero) write as a
  // valid outcome - e.g. src/picofuse/sys/pico/stdio_uart.c's ring buffer
  // returns 0 once full, relying on its IRQ handler to drain it as the
  // real UART FIFO empties. A single fire-and-forget call here silently
  // dropped output whenever a backend's buffer filled mid-printf - retry
  // until the byte is actually accepted instead. Interrupts stay enabled
  // during this (printf only holds its own mutex, not a critical
  // section), so the drain this is waiting on can still run.
  //
  // A NULL stream is the one case sys_iostream_write() documents as
  // permanently returning 0 rather than "not yet" - guard it explicitly
  // so that case still no-ops instead of spinning forever.
  if (state->stream == NULL) {
    return 0;
  }
  while (sys_iostream_write(state->stream, &ch, 1) == 0) {
  }
  return 1;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Prints formatted output to the console, holding the printf mutex
 * for the whole call so concurrent sys_vprintf_ex() calls can't interleave.
 * sys_printf() and _sys_debugf_impl() both delegate to sys_vprintf() (and
 * thus here) rather than locking themselves. */
size_t sys_vprintf_ex(const char *format, va_list args,
                      sys_printf_format_handler_t custom_handler) {
  struct sys_printf_state state = {.putch = _sys_printf_putch,
                                   .custom = custom_handler};
  va_list args_copy;
  va_copy(args_copy, args);

  sys_mutex_lock(_sys_printf_mutex);
  size_t len = _sys_vprintf(&state, format, &args_copy);
  sys_mutex_unlock(_sys_printf_mutex);

  va_end(args_copy);
  return len;
}

size_t sys_vprintf(const char *format, va_list args) {
  return sys_vprintf_ex(format, args, NULL);
}

size_t sys_vfprintf(sys_iostream_t *stream, const char *format, va_list args) {
  if (stream == NULL || format == NULL) {
    return 0;
  }

  struct sys_printf_state state = {.putch = _sys_fprintf_putch,
                                   .stream = stream};
  va_list args_copy;
  va_copy(args_copy, args);

  sys_mutex_lock(_sys_printf_mutex);
  size_t len = _sys_vprintf(&state, format, &args_copy);
  sys_mutex_unlock(_sys_printf_mutex);

  va_end(args_copy);
  return len;
}

/** @brief Prints formatted output to a string buffer. Writes to caller-owned
 * memory rather than the shared console, so unlike sys_vprintf_ex() this
 * does not take the printf mutex. sys_sprintf_ex() delegates here. */
size_t sys_vsprintf_ex(char *buf, size_t sz, const char *format, va_list args,
                       sys_printf_format_handler_t custom_handler) {
  struct sys_printf_state state = {.putch = _sys_sprintf_putch,
                                   .buffer = buf,
                                   .size = sz,
                                   .custom = custom_handler};
  va_list args_copy;
  va_copy(args_copy, args);
  size_t len = _sys_vprintf(&state, format, &args_copy);
  va_end(args_copy);

  // Null terminate the buffer
  if (buf && sz > 0) {
    buf[state.pos < sz - 1 ? state.pos : sz - 1] = '\0';
  }

  return len;
}

size_t sys_vsprintf(char *buf, size_t sz, const char *format, va_list args) {
  return sys_vsprintf_ex(buf, sz, format, args, NULL);
}

size_t sys_sprintf(char *buf, size_t sz, const char *format, ...) {
  va_list args;
  va_start(args, format);
  size_t len = sys_vsprintf(buf, sz, format, args);
  va_end(args);
  return len;
}

size_t sys_sprintf_ex(char *buf, size_t sz, const char *format,
                      sys_printf_format_handler_t custom_handler, ...) {
  va_list args;
  va_start(args, custom_handler);
  size_t len = sys_vsprintf_ex(buf, sz, format, args, custom_handler);
  va_end(args);
  return len;
}
