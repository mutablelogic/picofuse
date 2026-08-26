#include "picofuse/sys/printf.h"

// Stub: sys_vprintf_ex/sys_vsprintf_ex are the only two functions with real
// work to do (produce output); everything else just forwards to them, the
// same layering a real implementation would use. sys_puts/sys_putch live in
// their own puts.c.

size_t sys_vprintf_ex(const char *format, va_list args,
                       sys_printf_format_handler_t custom_handler) {
  (void)format;
  (void)args;
  (void)custom_handler;
  return 0;
}

size_t sys_vsprintf_ex(char *buf, size_t sz, const char *format,
                        va_list args,
                        sys_printf_format_handler_t custom_handler) {
  (void)buf;
  (void)sz;
  (void)format;
  (void)args;
  (void)custom_handler;
  return 0;
}

size_t sys_vprintf(const char *format, va_list args) {
  return sys_vprintf_ex(format, args, NULL);
}

size_t sys_vsprintf(char *buf, size_t sz, const char *format, va_list args) {
  return sys_vsprintf_ex(buf, sz, format, args, NULL);
}

size_t sys_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  size_t n = sys_vprintf(format, args);
  va_end(args);
  return n;
}

size_t sys_sprintf(char *buf, size_t sz, const char *format, ...) {
  va_list args;
  va_start(args, format);
  size_t n = sys_vsprintf(buf, sz, format, args);
  va_end(args);
  return n;
}

size_t sys_sprintf_ex(char *buf, size_t sz, const char *format,
                       sys_printf_format_handler_t custom_handler, ...) {
  va_list args;
  va_start(args, custom_handler);
  size_t n = sys_vsprintf_ex(buf, sz, format, args, custom_handler);
  va_end(args);
  return n;
}
