/**
 * @file rune.h
 * @brief UTF-8 rune (Unicode code point) methods.
 * @defgroup SystemData Data types and utilities
 * @ingroup System
 *
 * Runes and strings (rune.h, string.h), the flag-driven scanner built on
 * them (scanner.h), and the stream I/O they read from (io.h).
 */

/**
 * @brief UTF-8 rune (Unicode code point) methods.
 * @defgroup SystemDataRune Runes
 * @ingroup SystemData
 */

#pragma once
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A single decoded Unicode code point. */
typedef int32_t rune_t;

/**
 * @brief Sentinel value for a malformed UTF-8 sequence.
 * @ingroup SystemDataRune
 *
 * This is the Unicode replacement character (U+FFFD), returned in `rune`
 * when the bytes at the current position do not form a valid UTF-8
 * sequence. The returned pointer still advances (past the offending byte),
 * so iteration can continue.
 */
#define RUNE_ERROR ((rune_t)0xFFFD)

/**
 * @brief Decode the next UTF-8 rune from a string.
 * @ingroup SystemDataRune
 * @param str Pointer to a null-terminated UTF-8 string.
 * @param rune Pointer to store the decoded rune. Set to RUNE_ERROR on a
 * malformed sequence, or 0 if the string is exhausted.
 * @return Pointer to the byte following the consumed rune (pass this
 * back in as `str` for the next call), or NULL once the string is
 * exhausted. On a malformed sequence, the pointer still advances - by
 * exactly one byte - so a decode loop can keep going after an error.
 */
extern const char *sys_rune_next(const char *str, rune_t *rune);

/**
 * @brief Count the runes in a UTF-8 string.
 * @ingroup SystemDataRune
 * @param str Pointer to a null-terminated UTF-8 string, or NULL.
 * @return The number of runes decoded before the terminator (each
 * malformed byte sequence counts as one), or 0 if `str` is NULL or
 * empty. Use sys_rune_valid() to check well-formedness.
 */
extern size_t sys_rune_count(const char *str);

/**
 * @brief Check whether a string is well-formed UTF-8.
 * @ingroup SystemDataRune
 * @param str Pointer to a null-terminated UTF-8 string, or NULL.
 * @return true if every byte sequence up to the terminator decodes
 * without error, or if `str` is NULL or empty; false if any malformed
 * sequence is found.
 */
extern bool sys_rune_valid(const char *str);

///////////////////////////////////////////////////////////////////////////////
// CLASSIFICATION
//
// These classify ASCII plus the Latin-1 Supplement block (0x80-0xFF) -
// accented Western European letters and their punctuation/space/control
// characters are recognized, but every other script (Greek, Cyrillic, CJK,
// etc.) is out of scope and reports false from all of them.

/**
 * @brief Reports whether r is a decimal digit.
 * @ingroup SystemDataRune
 */
static inline bool sys_rune_is_digit(rune_t r) { return r >= '0' && r <= '9'; }

/**
 * @brief Reports whether r is a space character (space, \\t, \\n, \\v, \\f,
 * \\r, U+0085 NEL, or U+00A0 NBSP).
 * @ingroup SystemDataRune
 */
static inline bool sys_rune_is_space(rune_t r) {
  switch (r) {
  case ' ':
  case '\t':
  case '\n':
  case '\v':
  case '\f':
  case '\r':
  case 0x0085: // NEL
  case 0x00A0: // NBSP
    return true;
  default:
    return false;
  }
}

/**
 * @brief Reports whether r is an uppercase letter.
 * @ingroup SystemDataRune
 */
static inline bool sys_rune_is_upper(rune_t r) {
  if (r >= 'A' && r <= 'Z') {
    return true;
  }
  if (r >= 0x00C0 && r <= 0x00D6) { // A grave - O diaeresis
    return true;
  }
  if (r >= 0x00D8 && r <= 0x00DE) { // O stroke - Thorn
    return true;
  }
  return false;
}

/**
 * @brief Reports whether r is a lowercase letter.
 * @ingroup SystemDataRune
 */
static inline bool sys_rune_is_lower(rune_t r) {
  if (r >= 'a' && r <= 'z') {
    return true;
  }
  if (r == 0x00DF) { // sharp s
    return true;
  }
  if (r >= 0x00E0 && r <= 0x00F6) { // a grave - o diaeresis
    return true;
  }
  if (r >= 0x00F8 && r <= 0x00FF) { // o stroke - y diaeresis
    return true;
  }
  return false;
}

/**
 * @brief Reports whether r is a letter.
 * @ingroup SystemDataRune
 */
static inline bool sys_rune_is_alpha(rune_t r) {
  return sys_rune_is_upper(r) || sys_rune_is_lower(r);
}

/**
 * @brief Converts r to its uppercase form, if it has one.
 * @ingroup SystemDataRune
 * @param r The rune to convert.
 * @return The uppercase form of r, or r unchanged if it has none (this
 * includes sharp s and y diaeresis, which have no single-codepoint
 * uppercase form).
 */
static inline rune_t sys_rune_to_upper(rune_t r) {
  if (r >= 'a' && r <= 'z') {
    return r - 32;
  }
  if (r >= 0x00E0 && r <= 0x00F6) { // a grave - o diaeresis
    return r - 0x20;
  }
  if (r >= 0x00F8 && r <= 0x00FE) { // o stroke - thorn
    return r - 0x20;
  }
  return r;
}

/**
 * @brief Converts r to its lowercase form, if it has one.
 * @ingroup SystemDataRune
 * @param r The rune to convert.
 * @return The lowercase form of r, or r unchanged if it has none.
 */
static inline rune_t sys_rune_to_lower(rune_t r) {
  if (r >= 'A' && r <= 'Z') {
    return r + 32;
  }
  if (r >= 0x00C0 && r <= 0x00D6) { // A grave - O diaeresis
    return r + 0x20;
  }
  if (r >= 0x00D8 && r <= 0x00DE) { // O stroke - Thorn
    return r + 0x20;
  }
  return r;
}

/**
 * @brief Reports whether r is a punctuation character.
 * @ingroup SystemDataRune
 *
 * Follows Unicode's Punctuation category, not the C locale's broader
 * ispunct() - math/currency/modifier symbols are sys_rune_is_symbol()
 * instead.
 */
static inline bool sys_rune_is_punct(rune_t r) {
  switch (r) {
  case '!':
  case '"':
  case '#':
  case '%':
  case '&':
  case '\'':
  case '(':
  case ')':
  case '*':
  case ',':
  case '-':
  case '.':
  case '/':
  case ':':
  case ';':
  case '?':
  case '@':
  case '[':
  case '\\':
  case ']':
  case '_':
  case '{':
  case '}':
  case 0x00A1: // inverted exclamation mark
  case 0x00A7: // section sign
  case 0x00B6: // pilcrow sign
  case 0x00B7: // middle dot
  case 0x00BF: // inverted question mark
    return true;
  default:
    return false;
  }
}

/**
 * @brief Reports whether r is a symbol character (math, currency, or
 * modifier symbols).
 * @ingroup SystemDataRune
 *
 * The complement of sys_rune_is_punct() within the printable
 * ASCII/Latin-1 range - together they partition it.
 */
static inline bool sys_rune_is_symbol(rune_t r) {
  switch (r) {
  case '$':
  case '+':
  case '<':
  case '=':
  case '>':
  case '^':
  case '`':
  case '|':
  case '~':
  case 0x00A2: // cent sign
  case 0x00A3: // pound sign
  case 0x00A4: // currency sign
  case 0x00A5: // yen sign
  case 0x00A6: // broken bar
  case 0x00A8: // diaeresis
  case 0x00A9: // copyright sign
  case 0x00AC: // not sign
  case 0x00AE: // registered sign
  case 0x00AF: // macron
  case 0x00B0: // degree sign
  case 0x00B1: // plus-minus sign
  case 0x00B4: // acute accent
  case 0x00B8: // cedilla
  case 0x00D7: // multiplication sign
  case 0x00F7: // division sign
    return true;
  default:
    return false;
  }
}

/**
 * @brief Reports whether r is a control character.
 * @ingroup SystemDataRune
 */
static inline bool sys_rune_is_control(rune_t r) {
  if (r >= 0x00 && r <= 0x1F) {
    return true;
  }
  if (r == 0x7F) { // DEL
    return true;
  }
  if (r >= 0x80 && r <= 0x9F) { // C1 controls
    return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// TOKENIZATION

/**
 * @brief Classification of a rune, or of a maximal run of same-class runes.
 * @ingroup SystemDataRune
 */
typedef enum {
  sys_rune_other = 0, // unclassified - also covers RUNE_ERROR
  sys_rune_space,
  sys_rune_digit,
  sys_rune_alpha,
  sys_rune_punct,
  sys_rune_symbol,
  sys_rune_control,
} sys_rune_class_t;

/**
 * @brief Classify a single rune.
 * @ingroup SystemDataRune
 * @param r The rune to classify.
 * @return The first matching classification (space, control, digit,
 * alpha, punct, symbol, in that order), or sys_rune_other if none match.
 * '\t' '\n' '\v' '\f' '\r' and NEL match both sys_rune_is_space() and
 * sys_rune_is_control() - space wins, so sys_rune_control ends up
 * covering only genuinely non-whitespace control codes.
 */
extern sys_rune_class_t sys_rune_isa(rune_t r);

/**
 * @brief Tokenizer state, grouping a stream into maximal runs of
 * same-class runes.
 * @ingroup SystemDataRune
 *
 * Reads from the sys_iostream_t given to sys_rune_tokenize_init().
 * Records only where the current token starts and how long it is - the
 * stream is the storage, so there's no capacity limit or allocation. Use
 * sys_rune_tokenize_token() to read its actual text.
 */
typedef struct sys_rune_tokenize_t {
  sys_iostream_t *stream; ///< source (private)
  ptrdiff_t start;        ///< absolute stream position where the current token begins
  size_t bytes;           ///< length of the current token, in bytes
  size_t runes;           ///< number of runes in the current token
  sys_rune_class_t isa;   ///< classification shared by the whole token
} sys_rune_tokenize_t;

/**
 * @brief Initialize a tokenizer over a stream.
 * @ingroup SystemDataRune
 * @param stream The stream to read runes from, or NULL.
 * @return An initialized tokenizer, positioned before the first token.
 */
extern sys_rune_tokenize_t sys_rune_tokenize_init(sys_iostream_t *stream);

/**
 * @brief Advance to the next maximal run of same-class runes.
 * @ingroup SystemDataRune
 * @param it Pointer to the tokenizer state.
 * @return true if a token was found (it->start, it->bytes, it->runes and
 * it->isa are populated), false if the stream is exhausted.
 */
extern bool sys_rune_tokenize_next(sys_rune_tokenize_t *it);

/**
 * @brief Read the current token's text.
 * @ingroup SystemDataRune
 * @param it The tokenizer, positioned at a token by a prior successful
 * sys_rune_tokenize_next() call.
 * @param buf Destination buffer.
 * @param cap Capacity of buf.
 * @return The number of bytes copied - min(it->bytes, cap); cap == 0 or
 * too small silently truncates rather than failing.
 */
extern size_t sys_rune_tokenize_token(sys_rune_tokenize_t *it, char *buf,
                                      size_t cap);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
