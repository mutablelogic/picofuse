/**
 * @file scanner.h
 * @brief Flag-driven tokenizer: turns a byte stream into a sequence of
 * typed tokens, one sys_scanner_next() call at a time.
 * @defgroup SystemDataScanner Scanner
 * @ingroup SystemData
 *
 * A sys_scanner_t reads from a sys_iostream_t (sys/io.h) and classifies
 * each rune using sys/rune.h's classification, extending a run of
 * same-class runes (spaces, letters, digits) into one token. Which extra
 * token shapes it also recognizes - identifiers, quoted strings,
 * numbers, comments, newlines - is controlled entirely by the
 * sys_scanner_flags_t passed to sys_scanner_init(); with no flags set,
 * tokens are just the base rune classes.
 *
 * A token's text is never copied: sys_scanner_t only records where it
 * starts and how long it is, reading it back from the stream on demand
 * with sys_scanner_token(). Scanning is allocation-free and works over a
 * stream of any length.
 *
 * Example - scan `"x = 42 // answer"`, recognizing identifiers, numbers
 * and a trailing comment:
 * @code
 * sys_iostream_t *s = sys_string_read("x = 42 // answer");
 * sys_scanner_t it = sys_scanner_init(
 *     s, sys_scanner_keywords | sys_scanner_numbers |
 *            sys_scanner_comments_slash);
 *
 * char buf[64];
 * while (sys_scanner_next(&it)) {
 *   size_t n = sys_scanner_token(&it, buf, sizeof(buf) - 1);
 *   buf[n] = '\0';
 *   printf("[%d] \"%s\"\n", it.isa, buf); // it.isa: a sys_scanner_class_t
 * }
 * sys_iostream_close(s);
 * @endcode
 * Tokens: `"x"` (keyword), `" "`, `"="` (symbol), `" "`, `"42"` (number),
 * `" "`, `"// answer"` (comment).
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
 * @ingroup SystemDataScanner
 *
 * sys_scanner_space/digit/alpha (and the flag-added keyword/string/
 * comment/number) extend over a maximal run of matching runes.
 * sys_scanner_punct/symbol/newline/control are always exactly one rune,
 * never a run - structural characters like '{'/'}'/',' stay individually
 * addressable, and counting occurrences (e.g. newlines) is just counting
 * tokens.
 */
typedef enum {
  sys_scanner_other = 0, ///< unclassified or malformed
  sys_scanner_space,     ///< whitespace run - see sys_rune_is_space()
  sys_scanner_digit,     ///< decimal digit run
  sys_scanner_alpha,     ///< letter run
  sys_scanner_punct,     ///< one punctuation character
  sys_scanner_symbol,    ///< one symbol character
  sys_scanner_control,   ///< one control character (non-whitespace)
  sys_scanner_escape,    ///< a \-escape sequence - sys_scanner_escapes
  sys_scanner_newline,   ///< one '\n' - sys_scanner_newlines
  sys_scanner_keyword,   ///< an identifier - sys_scanner_keywords
  sys_scanner_string,    ///< a quoted string - sys_scanner_quotes
  sys_scanner_comment,   ///< a comment - sys_scanner_comments
  sys_scanner_number,    ///< a number - sys_scanner_numbers
} sys_scanner_class_t;

/**
 * @brief Flags controlling which tokenization rules sys_scanner_init()
 * applies.
 * @ingroup SystemDataScanner
 *
 * Combine with bitwise OR. A "_with..."/specific-form flag (e.g.
 * sys_scanner_keywords_withunderscores, sys_scanner_numbers_hex) already
 * includes its base flag - no need to OR them together.
 */
typedef enum {
  sys_scanner_none = 0,

  /** Recognize \\" \\\\ \\/ \\b \\f \\n \\r \\t and \\uXXXX as a single
   * sys_scanner_escape token instead of ordinary punctuation. An
   * unrecognized escape falls back to punctuation. */
  sys_scanner_escapes = 1 << 1,

  /** Give '\n' its own sys_scanner_newline class instead of merging it
   * into a sys_scanner_space run - useful for counting or finding
   * lines. */
  sys_scanner_newlines = 1 << 2,

  /** Recognize `[A-Za-z][A-Za-z0-9]*` identifiers as a single
   * sys_scanner_keyword token instead of separate alpha/digit runs. */
  sys_scanner_keywords = 1 << 3,

  /** sys_scanner_keywords, plus '_' allowed within an identifier. */
  sys_scanner_keywords_withunderscores = sys_scanner_keywords | (1 << 4),

  /** sys_scanner_keywords, plus '-' allowed within an identifier.
   * Combine with sys_scanner_keywords_withunderscores to allow both. */
  sys_scanner_keywords_withdashes = sys_scanner_keywords | (1 << 5),

  /** Recognize '...'-quoted strings as sys_scanner_string. '\' escapes
   * whatever rune follows it; an unterminated string runs to end of
   * stream (check whether the token's last byte is the quote). */
  sys_scanner_quotes_single = 1 << 6,

  /** Recognize "..."-quoted strings - same rules as
   * sys_scanner_quotes_single, triggered by '"' instead. */
  sys_scanner_quotes_double = 1 << 7,

  /** Both quote styles - sys_scanner_quotes_single | _double. */
  sys_scanner_quotes = sys_scanner_quotes_single | sys_scanner_quotes_double,

  /** Recognize '#' line comments as sys_scanner_comment, running to (not
   * including) the next '\n' or end of stream. */
  sys_scanner_comments_hash = 1 << 8,

  /** Recognize '//' line comments - same rules as
   * sys_scanner_comments_hash, triggered by "//" instead. */
  sys_scanner_comments_slash = 1 << 9,

  /** Both comment styles - sys_scanner_comments_hash | _slash. */
  sys_scanner_comments = sys_scanner_comments_hash | sys_scanner_comments_slash,

  /** Recognize `[+-]?[0-9]+` as a single sys_scanner_number token
   * instead of a separate sign and digit run. */
  sys_scanner_numbers = 1 << 10,

  /** sys_scanner_numbers, plus octal: "0o"/"0O" (Python/Rust/Swift
   * style, self-describing) or a bare leading zero (C style, not
   * self-describing - indistinguishable from an ordinary decimal number
   * without knowing this flag was set) followed by octal digits, e.g.
   * "0o17" or "0755". */
  sys_scanner_numbers_octal = sys_scanner_numbers | (1 << 11),

  /** sys_scanner_numbers, plus "0b"/"0B" followed by binary digits,
   * e.g. "0b0101". */
  sys_scanner_numbers_binary = sys_scanner_numbers | (1 << 12),

  /** sys_scanner_numbers, plus "0x"/"0X" followed by hex digits, e.g.
   * "0x1A2f". */
  sys_scanner_numbers_hex = sys_scanner_numbers | (1 << 13),

  /** sys_scanner_numbers, plus a fractional part ("3.14") and/or an
   * exponent ("1e10", "1.5e-3"). Doesn't apply to octal/binary/hex
   * numbers. */
  sys_scanner_numbers_float = sys_scanner_numbers | (1 << 14),
} sys_scanner_flags_t;

/**
 * @brief Scanner state: reads a stream into a sequence of typed tokens.
 * @ingroup SystemDataScanner
 *
 * Records only where the current token starts and how long it is - the
 * stream itself is the storage, so there's no capacity limit or
 * allocation. Use sys_scanner_token() to read its actual text.
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
 * @ingroup SystemDataScanner
 * @param stream The stream to read from, or NULL.
 * @param flags Bitwise OR of sys_scanner_flags_t values controlling
 * which tokenization rules apply.
 * @return An initialized scanner, positioned before the first token.
 */
extern sys_scanner_t sys_scanner_init(sys_iostream_t *stream,
                                      sys_scanner_flags_t flags);

/**
 * @brief Advance to the next token.
 * @ingroup SystemDataScanner
 * @param it Pointer to the scanner state.
 * @return true if a token was found (it->start, it->bytes, it->runes and
 * it->isa are populated), false if the stream is exhausted.
 */
extern bool sys_scanner_next(sys_scanner_t *it);

/**
 * @brief Read the current token's text.
 * @ingroup SystemDataScanner
 * @param it The scanner, positioned at a token by a prior successful
 * sys_scanner_next() call.
 * @param buf Destination buffer.
 * @param cap Capacity of buf.
 * @return The number of bytes copied - min(it->bytes, cap); cap == 0 or
 * too small silently truncates rather than failing.
 */
extern size_t sys_scanner_token(sys_scanner_t *it, char *buf, size_t cap);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
