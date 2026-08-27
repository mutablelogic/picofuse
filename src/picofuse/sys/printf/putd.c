#include "types.h"
#include <stdarg.h> // for va_list, va_arg
#include <stddef.h> // for size_t, ptrdiff_t
#include <stdint.h> // for uint32_t, uint64_t, int32_t, int64_t

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Get the character representation of a digit for unsigned values.
 *
 * @param num The digit to convert (always < 16).
 * @param flags The printf flags to determine uppercase/lowercase for hex
 * digits.
 * @return The character representation of the digit.
 */
static inline char _sys_printf_putuv_digit(unsigned num,
                                            sys_printf_flags_t flags) {
  if (flags & SYS_PRINTF_FLAG_UPPER) {
    return (num < 10) ? '0' + num : 'A' + (num - 10);
  } else {
    return (num < 10) ? '0' + num : 'a' + (num - 10);
  }
}

/** @brief Negates a 32-bit signed value into its unsigned magnitude, setting
 * the NEG flag if it was negative. Safe against INT32_MIN overflow. */
static inline uint32_t _sys_printf_abs32(int32_t num,
                                          sys_printf_flags_t *flags) {
  if (num < 0) {
    *flags |= SYS_PRINTF_FLAG_NEG;
    return (uint32_t)(-(num + 1)) + 1u;
  }
  return (uint32_t)num;
}

/** @brief Negates a 64-bit signed value into its unsigned magnitude, setting
 * the NEG flag if it was negative. Safe against INT64_MIN overflow. */
static inline uint64_t _sys_printf_abs64(int64_t num,
                                          sys_printf_flags_t *flags) {
  if (num < 0) {
    *flags |= SYS_PRINTF_FLAG_NEG;
    return (uint64_t)(-(num + 1)) + UINT64_C(1);
  }
  return (uint64_t)num;
}

/** @brief Convert a 32-bit unsigned integer to a string representation based
 * on the printf state.
 *
 * @param state The printf state containing flags and width information.
 * @param num The 32-bit unsigned integer to convert.
 * @return The number of characters written to the output.
 */
size_t _sys_printf_putuv(struct sys_printf_state *state, uint32_t num) {
  // buffer[64] comfortably covers the 32-bit worst case (32 binary digits +
  // 2-char prefix + sign + null); callers must not pass a value that may
  // exceed 32 bits — use _sys_printf_putuv64() for that.
  char buffer[64];
  char *ptr = &buffer[63]; // Start from the end of the buffer
  *ptr = '\0';             // Null terminate

  int base = 10; // Default to decimal
  if (state->flags & SYS_PRINTF_FLAG_HEX) {
    base = 16; // Hexadecimal
  } else if (state->flags & SYS_PRINTF_FLAG_BIN) {
    base = 2; // Binary
  } else if (state->flags & SYS_PRINTF_FLAG_OCT) {
    base = 8; // Octal
  }

  // Convert number to string (in reverse order)
  size_t digits_written = 0;
  do {
    *--ptr = _sys_printf_putuv_digit((unsigned)(num % (uint32_t)base),
                                      state->flags);
    num /= (uint32_t)base;
    digits_written++;
  } while (num > 0);

  // Count prefix length for zero padding calculation
  size_t prefix_len = 0;
  if (state->flags & SYS_PRINTF_FLAG_NEG ||
      state->flags & SYS_PRINTF_FLAG_SIGN ||
      state->flags & SYS_PRINTF_FLAG_SPACE) {
    prefix_len++; // For +/- sign
  }
  if (state->flags & SYS_PRINTF_FLAG_PREFIX) {
    if (base == 16 || base == 2) {
      prefix_len += 2; // For 0x, 0X, 0b
    } else if (base == 8) {
      prefix_len += 1; // For 0
    }
  }

  // Zero-pad the number part if PAD flag is set and width is specified
  if ((state->flags & SYS_PRINTF_FLAG_PAD) &&
      state->width > (digits_written + prefix_len)) {
    size_t zero_pad_count = state->width - digits_written - prefix_len;
    for (size_t i = 0; i < zero_pad_count; i++) {
      *--ptr = '0';
    }
  }

  // If prefix add 0x, 0b or 0
  if (state->flags & SYS_PRINTF_FLAG_PREFIX) {
    if (base == 16) {
      if (state->flags & SYS_PRINTF_FLAG_UPPER) {
        *--ptr = 'X'; // Add 'X' for uppercase hexadecimal
      } else {
        *--ptr = 'x'; // Add 'x' for lowercase hexadecimal
      }
      *--ptr = '0'; // Add '0' for hexadecimal prefix
    } else if (base == 2) {
      *--ptr = 'b'; // Add 'b' for binary
      *--ptr = '0'; // Add '0' for binary prefix
    } else if (base == 8) {
      *--ptr = '0'; // Add '0' for octal prefix
    }
  }

  // Add sign prefix AFTER number conversion
  if (state->flags & SYS_PRINTF_FLAG_NEG) {
    *--ptr = '-'; // Add negative sign
  } else if (state->flags & SYS_PRINTF_FLAG_SIGN) {
    *--ptr = '+'; // Add positive sign
  } else if (state->flags & SYS_PRINTF_FLAG_SPACE) {
    *--ptr = ' '; // Add leading space for positive values
  }

  // Calculate string length for width padding
  size_t str_len = 0;
  char *temp_ptr = ptr;
  while (*temp_ptr) {
    str_len++;
    temp_ptr++;
  }

  size_t total_chars = 0;
  size_t padding = (state->width > str_len) ? state->width - str_len : 0;

  // Right-aligned by default (left padding)
  if (!(state->flags & SYS_PRINTF_FLAG_LEFT) && padding > 0) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  // Output the formatted number
  while (*ptr) {
    total_chars += state->putch(state, *ptr++);
  }

  // Left-aligned (right padding with spaces)
  if ((state->flags & SYS_PRINTF_FLAG_LEFT) && padding > 0) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  return total_chars;
}

/** @brief Convert a 64-bit unsigned integer to a string representation based
 * on the printf state.
 *
 * @param state The printf state containing flags and width information.
 * @param num The 64-bit unsigned integer to convert.
 * @return The number of characters written to the output.
 */
size_t _sys_printf_putuv64(struct sys_printf_state *state, uint64_t num) {
  // Worst case is 64 binary digits + 2-char prefix (0b) + optional sign/space
  // plus null terminator.
  char buffer[68];
  char *ptr = &buffer[67];
  *ptr = '\0';

  int base = 10;
  if (state->flags & SYS_PRINTF_FLAG_HEX) {
    base = 16;
  } else if (state->flags & SYS_PRINTF_FLAG_BIN) {
    base = 2;
  } else if (state->flags & SYS_PRINTF_FLAG_OCT) {
    base = 8;
  }

  size_t digits_written = 0;
  do {
    *--ptr = _sys_printf_putuv_digit((unsigned)(num % (uint64_t)base),
                                      state->flags);
    num /= (uint64_t)base;
    digits_written++;
  } while (num > 0u);

  size_t prefix_len = 0;
  if (state->flags & SYS_PRINTF_FLAG_NEG ||
      state->flags & SYS_PRINTF_FLAG_SIGN ||
      state->flags & SYS_PRINTF_FLAG_SPACE) {
    prefix_len++;
  }
  if (state->flags & SYS_PRINTF_FLAG_PREFIX) {
    if (base == 16 || base == 2) {
      prefix_len += 2;
    } else if (base == 8) {
      prefix_len += 1;
    }
  }

  if ((state->flags & SYS_PRINTF_FLAG_PAD) &&
      state->width > (digits_written + prefix_len)) {
    size_t zero_pad_count = state->width - digits_written - prefix_len;
    size_t available = (size_t)(ptr - buffer);
    if (zero_pad_count > available) {
      zero_pad_count = available;
    }
    for (size_t i = 0; i < zero_pad_count; i++) {
      *--ptr = '0';
    }
  }

  if (state->flags & SYS_PRINTF_FLAG_PREFIX) {
    if (base == 16) {
      *--ptr = (state->flags & SYS_PRINTF_FLAG_UPPER) ? 'X' : 'x';
      *--ptr = '0';
    } else if (base == 2) {
      *--ptr = 'b';
      *--ptr = '0';
    } else if (base == 8) {
      *--ptr = '0';
    }
  }

  if (state->flags & SYS_PRINTF_FLAG_NEG) {
    *--ptr = '-';
  } else if (state->flags & SYS_PRINTF_FLAG_SIGN) {
    *--ptr = '+';
  } else if (state->flags & SYS_PRINTF_FLAG_SPACE) {
    *--ptr = ' ';
  }

  size_t str_len = 0;
  char *temp_ptr = ptr;
  while (*temp_ptr) {
    str_len++;
    temp_ptr++;
  }

  size_t total_chars = 0;
  size_t padding = (state->width > str_len) ? state->width - str_len : 0;

  if (!(state->flags & SYS_PRINTF_FLAG_LEFT) && padding > 0) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  while (*ptr) {
    total_chars += state->putch(state, *ptr++);
  }

  if ((state->flags & SYS_PRINTF_FLAG_LEFT) && padding > 0) {
    for (size_t i = 0; i < padding; i++) {
      total_chars += state->putch(state, ' ');
    }
  }

  return total_chars;
}

/** @brief Convert an unsigned integer argument to a string representation
 * based on the printf state.
 *
 * @param state The printf state containing flags and width information.
 * @param va The argument list to read the value from.
 * @return The number of characters written to the output.
 *
 * SYS_PRINTF_FLAG_SIZET reads a size_t, which may be wider than 32 bits (it
 * is 64-bit on LP64 hosts like darwin/linux), so it's always routed through
 * the 64-bit path regardless of the host's actual size_t width.
 */
size_t _sys_printf_putu(struct sys_printf_state *state, va_list *va) {
  if (state->flags & SYS_PRINTF_FLAG_SIZET) {
    return _sys_printf_putuv64(state, (uint64_t)va_arg(*va, size_t));
  }
  if (state->flags & SYS_PRINTF_FLAG_LONG) {
    return _sys_printf_putuv64(state, va_arg(*va, uint64_t));
  }

  uint32_t num = (uint32_t)va_arg(*va, unsigned int);
  return _sys_printf_putuv(state, num);
}

/** @brief Convert a signed integer argument to a string representation
 * based on the printf state.
 *
 * @param state The printf state containing flags and width information.
 * @param va The argument list to read the value from.
 * @return The number of characters written to the output.
 *
 * SYS_PRINTF_FLAG_SIZET reads a ptrdiff_t, which may be wider than 32 bits
 * (it is 64-bit on LP64 hosts like darwin/linux), so it's always routed
 * through the 64-bit path regardless of the host's actual ptrdiff_t width.
 */
size_t _sys_printf_putd(struct sys_printf_state *state, va_list *va) {
  if (state->flags & SYS_PRINTF_FLAG_SIZET) {
    ptrdiff_t num = va_arg(*va, ptrdiff_t);
    return _sys_printf_putuv64(state, _sys_printf_abs64((int64_t)num,
                                                          &state->flags));
  }
  if (state->flags & SYS_PRINTF_FLAG_LONG) {
    int64_t num = va_arg(*va, int64_t);
    return _sys_printf_putuv64(state, _sys_printf_abs64(num, &state->flags));
  }

  int32_t num = va_arg(*va, int32_t);
  return _sys_printf_putuv(state, _sys_printf_abs32(num, &state->flags));
}
