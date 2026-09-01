#include <picofuse/sys/printf.h>
#include <stdarg.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Performs formatted output to the standard output.
 *
 * @param format The format string specifying how to format the output.
 * @param ... The variable arguments corresponding to the format specifiers.
 * @return The total number of characters written to the output.
 */
size_t sys_printf(const char *format, ...) {
  va_list va;
  va_start(va, format);
  size_t len = sys_vprintf(format, va);
  va_end(va);
  return len;
}

size_t sys_fprintf(sys_iostream_t *stream, const char *format, ...) {
  va_list va;
  va_start(va, format);
  size_t len = sys_vfprintf(stream, format, va);
  va_end(va);
  return len;
}
