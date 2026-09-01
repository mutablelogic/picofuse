/**
 * @file sys/printf.h
 * @brief Defines formatted output APIs for console and string buffers.
 * @defgroup SystemFormat Formatting
 * @ingroup SystemData
 *
 * This file declares types and functions for formatted output to the system
 * console or to a string buffer. It supports printf-style formatting with
 * various format specifiers, width modifiers, and flags.
 *
 * The sys_printf() function is the main entry point for formatted output,
 * and it supports a wide range of format specifiers, including integers,
 * strings, and characters. It also supports width modifiers for padding
 * and alignment, as well as flags for zero-padding, left-alignment, and
 * sign handling. There are also functions for formatted output of Objective-C
 * objects and NXTimeInterval values.
 *
 * The sys_sprintf() function is similar to sys_printf(), but it outputs
 * the formatted string to a buffer instead of the console. It also supports
 * the same format specifiers and flags, and it returns the number of characters
 * that would have been written to the buffer, not counting the null terminator.
 *
 * The format specifiers supported are:
 * - `%%c`: Character
 * - `%%s`: String
 * - `%@`: NXObject (Objective-C object). This only works if the Objective-C
 * runtime is available.
 * - `%%d`: Signed integer
 * - `%%u`: Unsigned integer
 * - `%%x`: Unsigned hexadecimal integer
 * - `%%b`: Unsigned binary integer
 * - `%%o`: Unsigned octal integer
 * - `%%X`: Uppercase hexadecimal integer
 * - `%%f`, `%%F`: Fixed-point floating-point value
 * - `%%e`, `%%E`: Exponential floating-point value
 * - `%%g`, `%%G`: Adaptive floating-point value
 * - `%%t`: NXTimeInterval (Objective-C time interval)
 * - `%%p`: Pointer address
 * - `%%q`: JSON-quoted Objective-C object
 * - `%%`: Literal percent sign
 *
 *
 * The format specifiers can be modified with flags and width:
 * - Width:
 *   - `%80d`: Number specifying the minimum width of the output
 *   - `%.3f`: Precision for floating-point output
 * - Data Type:
 *   - `%%ld`, `%%lu`: 64-bit signed/unsigned integer data type
 *   - `%%zu`, `%%zd`: size_t-sized unsigned and signed integers
 * - Flags:
 *   - `%-10s`: Left-aligned output
 *   - `%05d`: Zero-padded hexadecimal, decimal, octal, or binary output
 *   - `%+d`: Forces a sign for positive numbers
 *   - `% f`: Prefixes positive numeric output with a space
 *   - The alternate-form flag adds the usual base prefix for hexadecimal,
 *     binary, and octal output, and forces a decimal point for floating-point
 *     output where supported.
 *
 * The sys_printf() function is thread-safe and can be used from multiple
 * threads simultaneously without additional synchronization. It was implemented
 * to extend the use of printf to Objective-C objects and NXTimeInterval.
 *
 */
#pragma once
#include "io.h"
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Outputs a string to the system console or standard output.
 * @ingroup SystemFormat
 * @param str A pointer to a null-terminated string to be output. If `str` is
 * `NULL`, existing output is flushed.
 *
 * This function writes the specified null-terminated string to the
 * system's standard output stream, and flushes the output buffer.
 */
extern void sys_puts(const char *str);

/**
 * @brief Outputs a character to the system console or standard output.
 * @param ch The character to be output.
 * @ingroup SystemFormat
 *
 * This function writes the specified character to the
 * system's standard output stream, but does not flush the output buffer.
 */
extern void sys_putch(const char ch);

/**
 * @brief Prints formatted output to the system console.
 * @ingroup SystemFormat
 * @param format A printf-style format string that specifies how subsequent
 *               arguments are formatted and printed.
 * @param ... Additional arguments corresponding to format specifiers in format.
 * @return The number of characters printed.
 */
extern size_t sys_printf(const char *format, ...);

/**
 * @brief Prints formatted output using a va_list argument.
 * @ingroup SystemFormat
 * @param format A printf-style format string.
 * @param args A va_list containing the arguments for the format string.
 * @return The number of characters printed.
 */
extern size_t sys_vprintf(const char *format, va_list args);

/**
 * @brief Prints formatted output to a stream.
 * @ingroup SystemFormat
 * @param stream Destination stream.
 * @param format A printf-style format string.
 * @param ... Additional arguments corresponding to format specifiers in format.
 * @return The number of characters written.
 */
extern size_t sys_fprintf(sys_iostream_t *stream, const char *format, ...);

/**
 * @brief Prints formatted output to a stream using a va_list argument.
 * @ingroup SystemFormat
 * @param stream Destination stream.
 * @param format A printf-style format string.
 * @param args A va_list containing the arguments for the format string.
 * @return The number of characters written.
 */
extern size_t sys_vfprintf(sys_iostream_t *stream, const char *format,
                           va_list args);

/**
 * @brief Prints formatted output to a string buffer.
 * @ingroup SystemFormat
 * @param buf Pointer to the destination buffer where the formatted string
 *            will be stored. If NULL, only the length is calculated.
 * @param sz Size of the destination buffer in bytes, including space for
 *           the null terminator.
 * @param format A printf-style format string.
 * @param ... Additional arguments corresponding to format specifiers in format.
 * @return The number of characters that would have been written if the buffer
 *         was sufficiently large, not counting the null terminator.
 */
extern size_t sys_sprintf(char *buf, size_t sz, const char *format, ...);

/**
 * @brief Prints formatted output to a string buffer using a va_list argument.
 * @ingroup SystemFormat
 * @param buf Pointer to the destination buffer where the formatted string
 *            will be stored. If NULL, only the length is calculated.
 * @param sz Size of the destination buffer in bytes, including space for
 *           the null terminator.
 * @param format A printf-style format string.
 * @param args A va_list containing the arguments for the format string.
 * @return The number of characters that would have been written if the buffer
 *         was sufficiently large, not counting the null terminator.
 */
extern size_t sys_vsprintf(char *buf, size_t sz, const char *format,
                           va_list args);

/**
 * @brief Custom format handler function type.
 * @ingroup SystemFormat
 * @param format The format specifier character (e.g., '@' for '%@').
 * @param va Pointer to the va_list containing the arguments.
 * @return A string representation of the formatted value, or NULL if the
 *         format specifier is not handled by this custom handler.
 */
typedef const char *(*sys_printf_format_handler_t)(char format, va_list *va);

/**
 * @brief Prints formatted output using a va_list argument with custom format
 * handler support.
 * @ingroup SystemFormat
 * @param format A printf-style format string.
 * @param args A va_list containing the arguments for the format string.
 * @param custom_handler Custom format handler for unsupported format
 * specifiers, or NULL.
 * @return The number of characters printed.
 *
 * This function is similar to sys_vprintf() but allows specifying a custom
 * format handler that will be called for any format specifiers not handled
 * by the built-in implementation. This enables support for custom format
 * specifiers like '%@' for objects.
 */
extern size_t sys_vprintf_ex(const char *format, va_list args,
                             sys_printf_format_handler_t custom_handler);

/**
 * @brief Prints formatted output to a string buffer using a va_list argument
 * with custom format handler support.
 * @ingroup SystemFormat
 * @param buf Pointer to the destination buffer where the formatted string
 *            will be stored. If NULL, only the length is calculated.
 * @param sz Size of the destination buffer in bytes, including space for
 *           the null terminator.
 * @param format A printf-style format string.
 * @param args A va_list containing the arguments for the format string.
 * @param custom_handler Custom format handler for unsupported format
 * specifiers, or NULL.
 * @return The number of characters that would have been written if the buffer
 *         was sufficiently large, not counting the null terminator.
 *
 * This function is similar to sys_vsprintf() but allows specifying a custom
 * format handler that will be called for any format specifiers not handled
 * by the built-in implementation. This enables support for custom format
 * specifiers like '%@' for objects.
 */
extern size_t sys_vsprintf_ex(char *buf, size_t sz, const char *format,
                              va_list args,
                              sys_printf_format_handler_t custom_handler);

/**
 * @brief Prints formatted output to a string buffer with custom format
 * handler support.
 * @ingroup SystemFormat
 * @param buf Pointer to the destination buffer where the formatted string
 *            will be stored. If NULL, only the length is calculated.
 * @param sz Size of the destination buffer in bytes, including space for
 *           the null terminator.
 * @param format A printf-style format string.
 * @param custom_handler Custom format handler for unsupported format
 * specifiers, or NULL.
 * @param ... Additional arguments corresponding to format specifiers in format.
 * @return The number of characters that would have been written if the buffer
 *         was sufficiently large, not counting the null terminator.
 *
 * This function is similar to sys_sprintf() but allows specifying a custom
 * format handler that will be called for any format specifiers not handled
 * by the built-in implementation. This enables support for custom format
 * specifiers like '%@' for objects.
 */
extern size_t sys_sprintf_ex(char *buf, size_t sz, const char *format,
                             sys_printf_format_handler_t custom_handler, ...);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
