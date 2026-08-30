/**
 * @file string.h
 * @brief Whole-string operations built on the UTF-8 rune primitives in
 * sys/rune.h.
 * @defgroup SystemDataString Strings
 * @ingroup SystemData
 *
 * These operate on ordinary null-terminated `char *` strings, not a
 * dedicated string type - there's nothing to allocate or free beyond the
 * buffer you already have. The mutating functions (sys_string_to_upper(),
 * sys_string_trimspace(), sys_string_trimprefix(), sys_string_trimsuffix())
 * are all destructive, writing through the pointer you pass in, and all
 * return that same pointer back.
 *
 * Example - trim, check, and uppercase:
 * @code
 * char buf[] = "  hello world  ";
 * char *s = sys_string_trimspace(buf); // s == "hello world"
 * if (sys_string_hasprefix(s, "hello")) {
 *   sys_string_to_upper(s); // s == "HELLO WORLD", in place
 * }
 * @endcode
 */
#pragma once
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// METHODS

/**
 * @brief Return the length of a null-terminated string, in bytes.
 * @ingroup SystemDataString
 * @param str Pointer to a null-terminated string, or NULL.
 * @return The number of bytes before the terminator, or 0 if str is NULL.
 */
extern size_t sys_string_bytes(const char *str);

/**
 * @brief Count the runes in a null-terminated UTF-8 string.
 * @ingroup SystemDataString
 * @param str Pointer to a null-terminated UTF-8 string, or NULL.
 * @return The number of runes.
 */
extern size_t sys_string_runes(const char *str);

/**
 * @brief Compare two null-terminated strings byte by byte.
 * @ingroup SystemDataString
 * @param a First string, or NULL (treated as "").
 * @param b Second string, or NULL (treated as "").
 * @return < 0 if a sorts before b, 0 if they are equal, > 0 if a sorts
 * after b - the same three-way contract as the C library's strcmp().
 */
extern ptrdiff_t sys_string_compare(const char *a, const char *b);

/**
 * @brief Convert a string to uppercase in place.
 * @ingroup SystemDataString
 * @param str Pointer to a mutable, null-terminated UTF-8 string, or NULL.
 * @return str, for chaining.
 */
extern char *sys_string_to_upper(char *str);

/**
 * @brief Convert a string to lowercase in place.
 * @ingroup SystemDataString
 * @param str Pointer to a mutable, null-terminated UTF-8 string, or NULL.
 * @return str, for chaining.
 */
extern char *sys_string_to_lower(char *str);

/**
 * @brief Trim leading and trailing whitespace from a string in place.
 * @ingroup SystemDataString
 * @param str Pointer to a mutable, null-terminated UTF-8 string, or NULL.
 * @return str, for chaining, or NULL if the input was NULL.
 */
extern char *sys_string_trimspace(char *str);

/**
 * @brief Remove prefix from the start of s, if present.
 * @ingroup SystemDataString
 * @param s Pointer to a mutable, null-terminated string, or NULL.
 * @param prefix Prefix to remove, or NULL (treated as "").
 * @return s, for chaining, or NULL if the input was NULL.
 */
extern char *sys_string_trimprefix(char *s, const char *prefix);

/**
 * @brief Remove suffix from the end of s, if present.
 * @ingroup SystemDataString
 * @param s Pointer to a mutable, null-terminated string, or NULL.
 * @param suffix Suffix to remove, or NULL (treated as "").
 * @return s, truncated in place if it ended with suffix; s unchanged
 * otherwise. NULL in, NULL out.
 */
extern char *sys_string_trimsuffix(char *s, const char *suffix);

/**
 * @brief Reports whether s begins with prefix.
 * @ingroup SystemDataString
 * @param s String to check, or NULL (treated as "").
 * @param prefix Prefix to look for, or NULL (treated as "", which every
 * string has as a prefix).
 * @return true if s starts with the bytes of prefix.
 */
extern bool sys_string_hasprefix(const char *s, const char *prefix);

/**
 * @brief Reports whether s ends with suffix.
 * @ingroup SystemDataString
 * @param s String to check, or NULL (treated as "").
 * @param suffix Suffix to look for, or NULL (treated as "", which every
 * string has as a suffix).
 * @return true if s ends with the bytes of suffix.
 */
extern bool sys_string_hassuffix(const char *s, const char *suffix);

/**
 * @brief Find the first byte offset where substr occurs within s.
 * @ingroup SystemDataString
 * @param s String to search, or NULL (treated as "").
 * @param substr Substring to look for, or NULL (treated as "", which is
 * found at offset 0 in any string, including "").
 * @return The byte offset of the first occurrence of substr in s, or -1
 * if substr does not occur in s.
 */
extern ptrdiff_t sys_string_contains(const char *s, const char *substr);

/**
 * @brief Wrap a string in a read-only stream.
 * @ingroup SystemDataString
 * @param str Pointer to a null-terminated string, or NULL.
 * @return A stream reading str's bytes (no copy is made - str must stay
 * alive for the stream's lifetime), or NULL if str is NULL or the
 * sys_iostream_t pool is exhausted. Release with sys_iostream_close().
 */
extern sys_iostream_t *sys_string_read(const char *str);

/**
 * @brief Open a read/write stream backed by a caller-provided mutable
 * buffer.
 * @ingroup SystemDataString
 * @param buf The buffer to read from and write into (no copy is made -
 * buf must stay alive for the stream's lifetime). If it already holds a
 * NUL-terminated string within the first `cap` bytes, that's the starting
 * content (like sys_string_read()); otherwise it's treated as empty and
 * buf[0] is set to '\0'.
 * @param cap The buffer's total capacity in bytes, including room for a
 * trailing NUL terminator. Must be at least 1.
 * @return A new stream, or NULL if buf is NULL, cap is 0, or the
 * sys_iostream_t pool is exhausted. Release with sys_iostream_close().
 *
 * Reads and writes share one cursor, starting at position 0. Reads and
 * seeks never go past the current content length; only a write can move
 * that boundary, and only forward, up to (cap - 1) bytes - the last byte
 * of `cap` is always reserved for a NUL terminator, kept up to date after
 * every write, so buf is a valid, correctly terminated C string of
 * whatever's been written so far at any point, not just once writing is
 * done. Writing past the (cap - 1)-byte usable capacity doesn't grow the
 * buffer; sys_iostream_write() truncates and reports the actual number of
 * bytes written, the same convention as sys_sprintf().
 */
extern sys_iostream_t *sys_string_open(char *buf, size_t cap);

///////////////////////////////////////////////////////////////////////////////
// PARSING

/**
 * @brief Decode a JSON-style backslash escape sequence into the rune it
 * denotes.
 * @ingroup SystemDataString
 * @param str Pointer to the escape sequence, starting at the backslash
 * (e.g. as matched by sys_scanner_escapes - see sys/scanner.h).
 * @param len The escape's exact length in bytes (2 for \\" \\\\ \\/ \\b
 * \\f \\n \\r \\t, 6 for \\uXXXX), or 0 for str's length up to its own
 * NUL terminator. Either way, str must contain exactly one escape and
 * nothing else - anything past it (before len bytes, or before the
 * terminator when len is 0) is a parse error, not silently ignored. With
 * len set, str need not be NUL-terminated at all, and nothing past
 * str[len - 1] is read.
 * @param rune Pointer to store the decoded rune. Set to RUNE_ERROR on a
 * parse error.
 * @return true if str starts with a recognized escape of exactly len
 * bytes (when len is nonzero), false otherwise - including a \\uXXXX
 * that decodes to a lone UTF-16 surrogate (D800-DFFF), which isn't a
 * valid standalone rune.
 */
extern bool sys_string_parse_escape(const char *str, size_t len, rune_t *rune);

/**
 * @brief Parse "true" or "false" into a bool.
 * @ingroup SystemDataString
 * @param str String to parse, or NULL.
 * @param out Pointer to store the result. Left unchanged on a parse
 * error - there's no error sentinel for bool the way RUNE_ERROR is for
 * rune_t.
 * @return true if str is exactly "true" or "false" (case-sensitive,
 * nothing else in str), false otherwise - any other value, including
 * "True"/"FALSE" or trailing content, is a parse error.
 */
extern bool sys_string_parse_bool(const char *str, bool *out);

/**
 * @brief Decode a quoted string into its unescaped content.
 * @ingroup SystemDataString
 * @param str Pointer to the quoted string, starting at the opening
 * quote (' or ") - e.g. as matched by sys_scanner_quotes (see
 * sys/scanner.h). Always NUL-terminated, regardless of len.
 * @param len The quoted string's exact length in bytes, opening quote
 * through closing quote inclusive, or 0 for str's length up to its own
 * NUL terminator. Either way, str must contain exactly one complete,
 * closed quoted string and nothing else - anything past the closing
 * quote is a parse error, and so is never finding one.
 * @param out Destination buffer for the unescaped content, or NULL to
 * write nothing and just get the decoded length (cap is then ignored,
 * as if it were 0, regardless of what's passed). May also be str itself,
 * decoding in place - decoding never expands content (every escape's
 * decoded form is no longer than the escape it came from), so writing
 * into str as it's read never overtakes what's still being read.
 * @param cap Capacity of out.
 * @return The number of bytes written to out (min(actual, cap); cap ==
 * 0, out == NULL, or cap too small all silently truncate, same as
 * sys_scanner_token()), or -1
 * on a parse error: str doesn't start with ' or ", no closing quote was
 * found, or an escape inside is malformed. \\' is recognized here (it
 * isn't part of sys_string_parse_escape()'s JSON-derived table, but
 * quoted strings need it to escape a literal quote) in addition to
 * everything sys_string_parse_escape() recognizes; anything else after a
 * backslash is a parse error, not passed through literally.
 */
extern ptrdiff_t sys_string_parse_quoted(const char *str, size_t len, char *out,
                                         size_t cap);

/**
 * @brief Parse a signed 32-bit integer.
 * @ingroup SystemDataString
 * @param str Pointer to the number, optionally signed with a leading
 * '+' or '-'.
 * @param len The number's exact length in bytes, or 0 for str's length
 * up to its own NUL terminator. Either way, str must contain exactly
 * one number and nothing else - trailing content is a parse error, not
 * ignored. With len set, str need not be NUL-terminated at all.
 * @param value Pointer to store the result. Left unchanged on a parse
 * error - there's no error sentinel for int32_t the way RUNE_ERROR is
 * for rune_t.
 * @return true if str is a well-formed integer that fits in an
 * int32_t, false otherwise. Recognizes decimal ("123"), hex ("0x1A"),
 * octal ("0o17" or bare-leading-zero "0755"), and binary ("0b0101") -
 * the same forms sys_scanner_numbers_octal/_binary/_hex recognize (see
 * sys/scanner.h). A float (a '.' or exponent) or anything else that
 * isn't one of these forms is a parse error, never a lossy truncation -
 * and so is a value too large or small to fit in an int32_t.
 */
extern bool sys_string_parse_int32(const char *str, size_t len, int32_t *value);

/**
 * @brief Parse a signed 64-bit integer.
 * @ingroup SystemDataString
 * @param str Pointer to the number, optionally signed with a leading
 * '+' or '-'.
 * @param len The number's exact length in bytes, or 0 for str's length
 * up to its own NUL terminator. Either way, str must contain exactly
 * one number and nothing else. With len set, str need not be
 * NUL-terminated at all.
 * @param value Pointer to store the result. Left unchanged on a parse
 * error.
 * @return true if str is a well-formed integer that fits in an
 * int64_t, false otherwise - same recognized forms (and same
 * float/malformed/out-of-range rejection) as sys_string_parse_int32().
 */
extern bool sys_string_parse_int64(const char *str, size_t len, int64_t *value);

/**
 * @brief Parse an unsigned 32-bit integer.
 * @ingroup SystemDataString
 * @param str Pointer to the number. A leading '+' is accepted; a
 * leading '-' is always a parse error - even "-0" - there's no negative
 * representation of an unsigned value.
 * @param len The number's exact length in bytes, or 0 for str's length
 * up to its own NUL terminator. Either way, str must contain exactly
 * one number and nothing else. With len set, str need not be
 * NUL-terminated at all.
 * @param value Pointer to store the result. Left unchanged on a parse
 * error.
 * @return true if str is a well-formed, non-negative integer that fits
 * in a uint32_t, false otherwise - same recognized forms (decimal, hex,
 * octal, binary) and same float/malformed/trailing-content rejection as
 * sys_string_parse_int32().
 */
extern bool sys_string_parse_uint32(const char *str, size_t len, uint32_t *value);

/**
 * @brief Parse an unsigned 64-bit integer.
 * @ingroup SystemDataString
 * @param str Pointer to the number. A leading '+' is accepted; a
 * leading '-' is always a parse error - even "-0".
 * @param len The number's exact length in bytes, or 0 for str's length
 * up to its own NUL terminator. Either way, str must contain exactly
 * one number and nothing else. With len set, str need not be
 * NUL-terminated at all.
 * @param value Pointer to store the result. Left unchanged on a parse
 * error.
 * @return true if str is a well-formed, non-negative integer that fits
 * in a uint64_t, false otherwise - same recognized forms as
 * sys_string_parse_uint32().
 */
extern bool sys_string_parse_uint64(const char *str, size_t len, uint64_t *value);

/**
 * @brief Parse a 32-bit floating-point number.
 * @ingroup SystemDataString
 * @param str Pointer to the number, optionally signed with a leading
 * '+' or '-'. Recognizes ordinary decimal notation ("3.14", "-0.5",
 * ".5", "5.", "1e10", "1.5e-3") and the exact literals "NaN" and "Inf"
 * (optionally signed, e.g. "-Inf") - no hex/octal/binary floats, and no
 * other spelling of infinity/not-a-number ("inf", "Infinity", "nan" are
 * all parse errors, not accepted case-insensitively).
 * @param len The number's exact length in bytes, or 0 for str's length
 * up to its own NUL terminator. Either way, str must contain exactly
 * one number and nothing else. With len set, str need not be
 * NUL-terminated at all.
 * @param value Pointer to store the result. Left unchanged on a parse
 * error.
 * @return true if str is a well-formed number, false otherwise - a
 * lone '.' with no digit on either side of it ("+."), an 'e' with no
 * digit after it ("1e"), or any other malformed or trailing content is
 * a parse error. A magnitude too large or small for float overflows/
 * underflows to +-Inf/0, the same as it would for
 * sys_string_parse_float64() - that's not a parse error, since a float
 * can represent it directly.
 */
extern bool sys_string_parse_float32(const char *str, size_t len, float *value);

/**
 * @brief Parse a 64-bit floating-point number.
 * @ingroup SystemDataString
 * @param str Pointer to the number, optionally signed with a leading
 * '+' or '-'. Same recognized forms as sys_string_parse_float32().
 * @param len The number's exact length in bytes, or 0 for str's length
 * up to its own NUL terminator. Either way, str must contain exactly
 * one number and nothing else. With len set, str need not be
 * NUL-terminated at all.
 * @param value Pointer to store the result. Left unchanged on a parse
 * error.
 * @return true if str is a well-formed number, false otherwise. Uses
 * double precision throughout, so this is exact; sys_string_parse_float32()
 * narrows the same parse to float afterward, which isn't always exact
 * for many-digit inputs (no more than a double itself can represent
 * exactly, around 17 significant decimal digits).
 */
extern bool sys_string_parse_float64(const char *str, size_t len, double *value);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
