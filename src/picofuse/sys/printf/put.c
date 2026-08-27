#include "types.h"
#include <stdarg.h> // for va_list, va_arg
#include <stddef.h> // for size_t
#include <stdint.h> // for uintptr_t

size_t _sys_printf_put(struct sys_printf_state *state, char spec, va_list *va) {
  switch (spec) {
  case 'c':
    return _sys_printf_putc(state, va); // Handle character output
  case 's':
    return _sys_printf_puts(state, va); // Handle string output
  case 'd':
    return _sys_printf_putd(state, va); // Handle signed decimal output
  case 'u':
    return _sys_printf_putu(state, va); // Handle unsigned decimal output
  case 'x':
  case 'X':
    state->flags |= SYS_PRINTF_FLAG_HEX; // Set hexadecimal flag
    if (spec == 'X') {
      state->flags |= SYS_PRINTF_FLAG_UPPER; // Set uppercase flag for 'X'
    }
    return _sys_printf_putu(state, va);
  case 'b':
    state->flags |= SYS_PRINTF_FLAG_BIN; // Set binary flag
    return _sys_printf_putu(state, va);
  case 'o':
    state->flags |= SYS_PRINTF_FLAG_OCT; // Set octal flag
    return _sys_printf_putu(state, va);
  case 'p': {
    // Handle pointer output with proper padding
    void *ptr = va_arg(*va, void *);
    uintptr_t ptr_value = (uintptr_t)ptr;
    state->flags |=
        SYS_PRINTF_FLAG_HEX | SYS_PRINTF_FLAG_PREFIX; // Set hex with 0x prefix
    const size_t ptr_hex_digits = sizeof(void *) * 2; // 2 hex digits per byte

    // For pointers, set width to hex digits + prefix length to get full padding
    size_t saved_width = state->width;
    state->width = ptr_hex_digits + 2;   // +2 for "0x" prefix
    state->flags |= SYS_PRINTF_FLAG_PAD; // Force zero padding for pointers

    size_t result = _sys_printf_putuv64(state, (uint64_t)ptr_value);

    state->width = saved_width; // Restore original width
    return result;
  }
  case 'f':
  case 'F':
  case 'e':
  case 'E':
  case 'g':
  case 'G':
    return _sys_printf_putf(state, spec, va);
  default:
    // If there is a custom format handler, use it
    if (state->custom) {
      const char *custom_result = state->custom(spec, va);
      if (custom_result) {
        size_t total_chars = 0;
        size_t len = 0;
        while (custom_result[len]) {
          total_chars += state->putch(state, custom_result[len]);
          len++;
        }
        return total_chars; // Return actual characters written
      }
    }
    return 0;
  }
}
