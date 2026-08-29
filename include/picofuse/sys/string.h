/**
 * @file string.h
 * @brief Whole-string operations built on the UTF-8 rune primitives in
 * sys/rune.h.
 * @ingroup SystemString
 */
#pragma once
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// METHODS

/**
 * @brief Return the length of a null-terminated string, in bytes.
 * @ingroup SystemString
 * @param str Pointer to a null-terminated string, or NULL.
 * @return The number of bytes before the terminator, or 0 if str is NULL.
 */
extern size_t sys_string_bytes(const char *str);

/**
 * @brief Count the runes in a null-terminated UTF-8 string.
 * @ingroup SystemString
 * @param str Pointer to a null-terminated UTF-8 string, or NULL.
 * @return The number of runes.
 */
extern size_t sys_string_runes(const char *str);

/**
 * @brief Compare two null-terminated strings byte by byte.
 * @ingroup SystemString
 * @param a First string, or NULL (treated as "").
 * @param b Second string, or NULL (treated as "").
 * @return < 0 if a sorts before b, 0 if they are equal, > 0 if a sorts
 * after b - the same three-way contract as the C library's strcmp().
 */
extern ptrdiff_t sys_string_compare(const char *a, const char *b);

/**
 * @brief Convert a string to uppercase in place.
 * @ingroup SystemString
 * @param str Pointer to a mutable, null-terminated UTF-8 string, or NULL.
 * @return str, for chaining.
 *
 * Destructive: overwrites str's bytes directly. Only runes within
 * sys_rune_is_lower()'s ASCII/Latin-1 range are converted; every other
 * rune, and any malformed byte, is left untouched.
 */
extern char *sys_string_to_upper(char *str);

/**
 * @brief Convert a string to lowercase in place.
 * @ingroup SystemString
 * @param str Pointer to a mutable, null-terminated UTF-8 string, or NULL.
 * @return str, for chaining.
 *
 * Destructive: overwrites str's bytes directly. Only runes within
 * sys_rune_is_upper()'s ASCII/Latin-1 range are converted; every other
 * rune, and any malformed byte, is left untouched.
 */
extern char *sys_string_to_lower(char *str);

/**
 * @brief Trim leading and trailing whitespace from a string in place.
 * @ingroup SystemString
 * @param str Pointer to a mutable, null-terminated UTF-8 string, or NULL.
 * @return A pointer into str's own buffer, past any leading whitespace
 * (as classified by sys_rune_is_space()). NULL in, NULL out.
 *
 * Destructive: writes exactly one null byte, at the new end of the
 * trimmed string, and returns an advanced pointer rather than moving any
 * bytes - use the returned pointer, not str, from this point on.
 */
extern char *sys_string_trimspace(char *str);

/**
 * @brief Remove prefix from the start of s, if present.
 * @ingroup SystemString
 * @param s Pointer to a mutable, null-terminated string, or NULL.
 * @param prefix Prefix to remove, or NULL (treated as "", which is
 * always present and removes nothing).
 * @return If s starts with prefix, a pointer into s's own buffer, past
 * those bytes; otherwise s itself, unchanged. NULL in, NULL out.
 *
 * Non-destructive: this only ever advances the returned pointer - it
 * never writes to s's buffer.
 */
extern char *sys_string_trimprefix(char *s, const char *prefix);

/**
 * @brief Remove suffix from the end of s, if present.
 * @ingroup SystemString
 * @param s Pointer to a mutable, null-terminated string, or NULL.
 * @param suffix Suffix to remove, or NULL (treated as "", which is
 * always present and removes nothing).
 * @return s, truncated in place if it ended with suffix; s unchanged
 * otherwise. NULL in, NULL out.
 *
 * Destructive: if suffix is present, writes a null byte where it began,
 * truncating s there.
 */
extern char *sys_string_trimsuffix(char *s, const char *suffix);

/**
 * @brief Reports whether s begins with prefix.
 * @ingroup SystemString
 * @param s String to check, or NULL (treated as "").
 * @param prefix Prefix to look for, or NULL (treated as "", which every
 * string has as a prefix).
 * @return true if s starts with the bytes of prefix.
 */
extern bool sys_string_hasprefix(const char *s, const char *prefix);

/**
 * @brief Reports whether s ends with suffix.
 * @ingroup SystemString
 * @param s String to check, or NULL (treated as "").
 * @param suffix Suffix to look for, or NULL (treated as "", which every
 * string has as a suffix).
 * @return true if s ends with the bytes of suffix.
 */
extern bool sys_string_hassuffix(const char *s, const char *suffix);

/**
 * @brief Find the first byte offset where substr occurs within s.
 * @ingroup SystemString
 * @param s String to search, or NULL (treated as "").
 * @param substr Substring to look for, or NULL (treated as "", which is
 * found at offset 0 in any string, including "").
 * @return The byte offset of the first occurrence of substr in s, or -1
 * if substr does not occur in s.
 */
extern ptrdiff_t sys_string_contains(const char *s, const char *substr);

/**
 * @brief Wrap a string in a read-only stream.
 * @ingroup SystemString
 * @param str Pointer to a null-terminated string, or NULL.
 * @return A stream reading str's bytes, or NULL if str is NULL or the
 * sys_iostream_t pool (SYS_IOSTREAM_CAPACITY) is exhausted. No copy of
 * str's bytes is made - str must stay alive and unmodified for the
 * stream's lifetime. Writing to this stream always reports 0 bytes
 * written (see sys_iostream_write()). Release with sys_iostream_close().
 */
extern sys_iostream_t *sys_string_read(const char *str);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
