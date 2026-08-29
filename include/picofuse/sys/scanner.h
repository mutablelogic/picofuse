/**
 * @file scanner.h
 * @brief Flag-driven tokenization of a stream into typed tokens, built
 * on the rune classification in sys/rune.h and the sys_iostream_t in
 * sys/io.h.
 * @defgroup SystemScanner Scanner
 * @ingroup System
 */
#pragma once
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Classification of a scanned token.
 * @ingroup SystemScanner
 *
 * Starts with the same six rune classes sys_rune_class_t has (plus
 * sys_scanner_other for anything sys_rune_isa() can't place, including
 * RUNE_ERROR). Further token types - identifiers, quoted strings,
 * numbers, comments, newlines - are added here as sys_scanner_init()
 * gains flags that recognize them.
 */
typedef enum {
  sys_scanner_other = 0,
  sys_scanner_space,
  sys_scanner_digit,
  sys_scanner_alpha,
  sys_scanner_punct,
  sys_scanner_symbol,
  sys_scanner_control,
  sys_scanner_escape, ///< a JSON-style \-escape sequence - see
                      ///< sys_scanner_escapes
  sys_scanner_newline, ///< a run of '\n' - see sys_scanner_newlines
  sys_scanner_keyword, ///< an identifier - see sys_scanner_keywords
  sys_scanner_string, ///< a '-quoted string, escapes left as-is - see
                      ///< sys_scanner_squote
} sys_scanner_class_t;

/**
 * @brief Flags controlling which tokenization rules sys_scanner_init()
 * applies.
 * @ingroup SystemScanner
 *
 * Combine with bitwise OR.
 */
typedef enum {
  sys_scanner_none = 0,

  /** Don't emit sys_scanner_space tokens - whitespace runs are skipped
   * between tokens instead of being returned as one. */
  sys_scanner_nospace = 1 << 0,

  /**
   * Recognize JSON-style backslash escape sequences as a single
   * sys_scanner_escape token, instead of tokenizing '\' as ordinary
   * punctuation:
   *
   *  - \" \\ \/ \b \f \n \r \t - a 2-byte token (backslash + the char).
   *  - \uXXXX, with exactly 4 hex digits (case-insensitive) - a 6-byte
   *    token.
   *
   * A backslash not followed by one of these - including a \u with fewer
   * than 4 hex digits, or one cut short by the end of the stream - is
   * not treated as an escape; it falls back to ordinary punctuation
   * classification. This only decides token boundaries: turning a
   * matched token's bytes into the rune it denotes is a separate step,
   * not yet provided.
   */
  sys_scanner_escapes = 1 << 1,

  /**
   * Classify '\n' as sys_scanner_newline, its own dedicated class,
   * rather than whatever it would otherwise fall under (sys_scanner_control,
   * since sys_rune_isa() checks control before space and '\n' (0x0A)
   * falls in the C0 control range - it was never actually
   * sys_scanner_space to begin with). A run of consecutive '\n's is one
   * sys_scanner_newline token, same "maximal same-class run" rule as
   * every other class. '\r' is unaffected and stays sys_scanner_control
   * (0x0D is in the same C0 range), so "\r\n" produces two tokens (a
   * one-byte control, then a newline).
   * Combines with sys_scanner_nospace: ordinary whitespace runs are
   * still skipped, but newlines are still reported.
   */
  sys_scanner_newlines = 1 << 2,

  /**
   * Recognize identifiers - `[A-Za-z][A-Za-z0-9]*` - as a single
   * sys_scanner_keyword token, instead of letting a leading run of
   * letters and a following run of digits fall out as two separate
   * sys_scanner_alpha/sys_scanner_digit tokens (as "abc123" would
   * without this flag). A token only ever starts as a keyword if its
   * first rune is a letter - digits alone are still sys_scanner_digit -
   * but that's a per-token rule, not "once digits, never keyword": in
   * "123abc" the digit run still ends at "123" (a sys_scanner_digit
   * token) and "abc" - starting with a letter - becomes its own
   * sys_scanner_keyword token right after it.
   */
  sys_scanner_keywords = 1 << 3,

  /**
   * Like sys_scanner_keywords (includes it - no need to OR the two
   * together), but also allows '_' within an identifier (not as the
   * first character) - "my_var123" becomes one sys_scanner_keyword
   * token instead of splitting at the underscore.
   */
  sys_scanner_keyword_withunderscores = sys_scanner_keywords | (1 << 4),

  /**
   * Like sys_scanner_keywords (includes it - no need to OR the two
   * together), but also allows '-' within an identifier (not as the
   * first character) - "my-var123" becomes one sys_scanner_keyword
   * token instead of splitting at the dash. Combine (OR) with
   * sys_scanner_keyword_withunderscores to allow both '_' and '-'.
   */
  sys_scanner_keyword_withdash = sys_scanner_keywords | (1 << 5),
} sys_scanner_flags_t;

/**
 * @brief Scanner state, grouping a stream into a sequence of typed
 * tokens according to its flags.
 * @ingroup SystemScanner
 *
 * Shaped like sys_rune_tokenize_t (sys/rune.h), with scanning rules
 * layered on top of plain same-class runs: rather than copying each
 * token's bytes into a buffer of its own, this records only where the
 * token starts and how long it is - the stream itself is the storage,
 * so there's no capacity limit and no heap allocation. To read a
 * token's actual text, use sys_scanner_token().
 */
typedef struct sys_scanner_t {
  sys_iostream_t *stream;    ///< source (private)
  sys_scanner_flags_t flags; ///< flags given to init() (private)
  ptrdiff_t start;           ///< absolute stream position where the current
                              ///< token begins
  size_t bytes;               ///< length of the current token, in bytes
  size_t runes;                ///< number of runes in the current token
  sys_scanner_class_t isa;     ///< classification of the current token
} sys_scanner_t;

///////////////////////////////////////////////////////////////////////////////
// METHODS

/**
 * @brief Initialize a scanner over a stream.
 * @ingroup SystemScanner
 * @param stream The stream to read from, or NULL.
 * @param flags Bitwise OR of sys_scanner_flags_t values controlling
 * which tokenization rules apply.
 * @return An initialized scanner, positioned before the first token.
 */
extern sys_scanner_t sys_scanner_init(sys_iostream_t *stream,
                                      sys_scanner_flags_t flags);

/**
 * @brief Advance to the next token.
 * @ingroup SystemScanner
 * @param it Pointer to the scanner state.
 * @return true if a token was found (it->start, it->bytes, it->runes and
 * it->isa are populated), false if the stream is exhausted.
 */
extern bool sys_scanner_next(sys_scanner_t *it);

/**
 * @brief Read the current token's text.
 * @ingroup SystemScanner
 * @param it The scanner, positioned at a token by a prior successful
 * sys_scanner_next() call.
 * @param buf Destination buffer.
 * @param cap Capacity of buf.
 * @return The number of bytes copied - min(it->bytes, cap); cap == 0 or
 * too small silently truncates rather than failing.
 *
 * Seeks the stream to it->start, reads, then seeks back to where it was
 * (just past the token, ready for the next sys_scanner_next() call) -
 * scanning can continue normally afterward.
 */
extern size_t sys_scanner_token(sys_scanner_t *it, char *buf, size_t cap);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
