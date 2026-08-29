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
 *
 * sys_scanner_space/digit/alpha extend over a maximal run of consecutive
 * runes of the same class, same as sys_scanner_keyword/string/comment/
 * number do over their own shape. sys_scanner_punct/symbol/newline/
 * control are the opposite: always exactly one rune, never a run, even
 * when the same character repeats or a different same-class character
 * follows immediately - "{}" is two tokens, not one, and so is "\n\n"
 * (with sys_scanner_newlines). This keeps every structural delimiter
 * (like '{'/'}'/','/'[' in a bracket-and-comma grammar such as JSON)
 * individually addressable instead of merging into an unpredictable
 * blob of adjacent punctuation, and (for newlines specifically) makes
 * counting how many occurred as simple as counting tokens of that class.
 * sys_scanner_control ends up covering only genuinely non-whitespace
 * control codes - '\t' '\n' '\v' '\f' '\r' and NEL are sys_rune_is_space()
 * as well as sys_rune_is_control(), and sys_rune_isa() resolves that
 * overlap to space (see its own doc comment), so those all fall under
 * sys_scanner_space instead, a run like any other whitespace.
 */
typedef enum {
  sys_scanner_other = 0, ///< unclassified - malformed bytes, and any
                         ///< codepoint sys_rune_isa() can't place
  sys_scanner_space,     ///< a run of whitespace - see sys_rune_is_space()
                         ///< (includes '\t' '\n' '\v' '\f' '\r' and NEL,
                         ///< not just literal spaces)
  sys_scanner_digit,     ///< a run of decimal digits
  sys_scanner_alpha,     ///< a run of letters
  sys_scanner_punct,     ///< a single punctuation character - never a run,
                         ///< see this enum's own doc comment above
  sys_scanner_symbol,    ///< a single symbol character - never a run, see
                         ///< this enum's own doc comment above
  sys_scanner_control,   ///< a single control character - never a run;
                         ///< excludes whitespace-like control codes like
                         ///< '\t', which are sys_scanner_space instead -
                         ///< see this enum's own doc comment above
  sys_scanner_escape, ///< a JSON-style \-escape sequence - see
                      ///< sys_scanner_escapes
  sys_scanner_newline, ///< a single '\n' - never a run - see
                       ///< sys_scanner_newlines
  sys_scanner_keyword, ///< an identifier - see sys_scanner_keywords
  sys_scanner_string, ///< a '- or "-quoted string, escapes left as-is -
                      ///< see sys_scanner_quotes
  sys_scanner_comment, ///< a comment - see sys_scanner_comments_hash/
                       ///< sys_scanner_comments_slash
  sys_scanner_number, ///< a whole number, optionally signed - see
                      ///< sys_scanner_numbers
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
   * rather than whatever it would otherwise fall under - ordinarily
   * sys_scanner_space, part of the same whitespace run as any
   * surrounding spaces/tabs (see sys_scanner_space's doc comment).
   * Unlike sys_scanner_space, a sys_scanner_newline token is always a
   * single '\n', never a run - "\n\n\n" is three tokens, so counting how
   * many newlines occurred is just counting how many times
   * sys_scanner_next() returns one, even where plain sys_scanner_space
   * would have merged them into one run of ordinary whitespace. '\r' is
   * unaffected and stays sys_scanner_space, so "\r\n" produces two
   * tokens (a space, then a newline) rather than merging into one -
   * without this flag they'd merge into a single space run instead.
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
  sys_scanner_keywords_withunderscores = sys_scanner_keywords | (1 << 4),

  /**
   * Like sys_scanner_keywords (includes it - no need to OR the two
   * together), but also allows '-' within an identifier (not as the
   * first character) - "my-var123" becomes one sys_scanner_keyword
   * token instead of splitting at the dash. Combine (OR) with
   * sys_scanner_keywords_withunderscores to allow both '_' and '-'.
   */
  sys_scanner_keywords_withdashes = sys_scanner_keywords | (1 << 5),

  /**
   * Recognize '-quoted strings as a single sys_scanner_string token,
   * from the opening quote through the closing one - instead of the
   * quote falling out as ordinary punctuation.
   *
   * A backslash inside the string escapes whatever single rune follows
   * it - that rune never ends the string, whatever it is. This isn't
   * limited to \' (though that's the case that matters for finding the
   * end): \\ has to work the same way, or "'a\\'" (content: a, then an
   * escaped backslash, then the closing quote) would be misread as the
   * backslash escaping the real closing quote and the string would
   * fail to end there. Escape sequences are left byte-for-byte in the
   * token; unescaping/unquoting is a separate, later step, not this
   * one's job.
   *
   * If the stream ends before an unescaped closing quote is found, the
   * token still ends there (everything from the opening quote to the
   * end of the stream) rather than failing - sys_scanner_next() has no
   * way to know in advance whether a closing quote exists later on
   * without abandoning streaming and searching ahead. Check whether
   * sys_scanner_token()'s last byte is '\'' to tell a properly closed
   * string from one that ran out of stream first.
   */
  sys_scanner_quotes_single = 1 << 6,

  /**
   * Recognize "-quoted strings as a single sys_scanner_string token -
   * the same rules as sys_scanner_quotes_single (backslash escapes
   * whatever follows it, unterminated-at-end-of-stream is best-effort,
   * check the token's last byte to tell the two apart), just triggered
   * by '"' instead of '\''. A '"' inside a '-quoted string (or a '\''
   * inside a "-quoted one) is ordinary content, not a terminator - each
   * kind of string only ever closes on its own matching quote
   * character. Combine (OR) with sys_scanner_quotes_single (or just use
   * sys_scanner_quotes) to recognize both kinds in the same stream.
   */
  sys_scanner_quotes_double = 1 << 7,

  /**
   * Recognize both '-quoted and "-quoted strings - equivalent to
   * sys_scanner_quotes_single | sys_scanner_quotes_double.
   */
  sys_scanner_quotes = sys_scanner_quotes_single | sys_scanner_quotes_double,

  /**
   * Recognize '#'-prefixed comments as a single sys_scanner_comment
   * token, instead of '#' falling out as ordinary punctuation. Runs
   * from the '#' through end of line or end of stream, whichever comes
   * first - the terminating '\n' itself is not included in the
   * comment; it's left for the next sys_scanner_next() call to
   * classify normally (as sys_scanner_newline if sys_scanner_newlines
   * is also set, otherwise whatever it would ordinarily be). Unlike a
   * quoted string, a comment has no "unterminated" case - running to
   * end of stream with no newline is a complete, ordinary comment.
   */
  sys_scanner_comments_hash = 1 << 8,

  /**
   * Recognize '//'-prefixed comments as a single sys_scanner_comment
   * token - the same rules as sys_scanner_comments_hash (runs to end of
   * line or end of stream, the newline itself isn't included, no
   * "unterminated" case), just triggered by two consecutive '/'
   * characters instead of one '#'. A single '/' not followed by a
   * second one is not a comment; it falls back to ordinary punctuation
   * (or symbol) classification.
   */
  sys_scanner_comments_slash = 1 << 9,

  /**
   * Recognize both '#' and '//' comment styles - equivalent to
   * sys_scanner_comments_hash | sys_scanner_comments_slash.
   */
  sys_scanner_comments = sys_scanner_comments_hash | sys_scanner_comments_slash,

  /**
   * Recognize whole numbers - `[+-]?[0-9]+` - as a single
   * sys_scanner_number token, instead of a bare digit run
   * (sys_scanner_digit) with any leading sign left as its own
   * punctuation/symbol token.
   *
   * A '+' or '-' only ever starts a number if at least one digit
   * immediately follows it; otherwise it falls back to ordinary
   * classification (symbol for '+', punct for '-'), same "commit only
   * what's confirmed" rule as escapes/comments. The scanner has no
   * lookback at the previous token, so it can't tell a leading sign
   * apart from a binary operator by context - "12-34" scans as two
   * number tokens ("12", "-34"), not a number, a punct, and a number.
   * If that ambiguity matters, resolve it at the parsing stage, which
   * has the context the scanner doesn't.
   *
   * Octal/binary/hex prefixes, floats, and exponential forms aren't
   * covered by this flag on its own - see sys_scanner_numbers_octal/
   * _binary/_hex/_float.
   */
  sys_scanner_numbers = 1 << 10,

  /**
   * Like sys_scanner_numbers (includes it - no need to OR the two
   * together), recognizing two spellings of an octal number as a single
   * sys_scanner_number token:
   *
   *  - "0o"/"0O" followed by one or more octal digits ('0'-'7') -
   *    Python/Rust/Swift style, e.g. "0o17". Self-describing the same
   *    way sys_scanner_numbers_hex/_binary's prefixes are: the marker
   *    only ever appears in an octal number, so a value parser can
   *    always tell this form apart from an ordinary decimal number just
   *    by looking at the token's bytes, regardless of which flags
   *    produced it.
   *  - a bare leading zero directly followed by octal digits, no marker
   *    - C style, e.g. "0755". Checked only if the prefixed form above
   *    didn't match. Unlike the prefixed form, this one is NOT
   *    self-describing - "0755" is byte-for-byte identical to what an
   *    ordinary decimal number with a leading zero would produce
   *    without this flag, so telling them apart later requires already
   *    knowing this flag was active when the token was scanned.
   *
   * Both forms only commit once a digit confirms them - "0o" or a bare
   * "0" with no octal digit after it falls back to the ordinary number
   * "0" plus whatever follows as its own token, same "commit only what's
   * confirmed" rule as escapes/hex/binary. Digit consumption stops at
   * the first non-octal-digit the same way every other run does - "0779"
   * (bare form) is the octal number "077" followed by its own decimal
   * number "9", not one token or a rejected literal.
   */
  sys_scanner_numbers_octal = sys_scanner_numbers | (1 << 11),

  /**
   * Like sys_scanner_numbers (includes it - no need to OR the two
   * together), but also recognizes "0b" or "0B" followed by one or more
   * binary digits ('0' or '1') as a single sys_scanner_number token,
   * e.g. "0b01010101". The prefix only commits if at least one binary
   * digit follows it - "0b" with nothing (or non-binary digits) after
   * falls back to the ordinary number "0", leaving "b..." for its own
   * token, same "commit only what's confirmed" rule as escapes.
   */
  sys_scanner_numbers_binary = sys_scanner_numbers | (1 << 12),

  /**
   * Like sys_scanner_numbers (includes it - no need to OR the two
   * together), but also recognizes "0x" or "0X" followed by one or more
   * hex digits ('0'-'9', 'a'-'f', 'A'-'F') as a single sys_scanner_number
   * token, e.g. "0x1A2f". Same fallback rule as
   * sys_scanner_numbers_binary: the prefix only commits if a hex digit
   * follows it, otherwise it's just the number "0" plus whatever "x..."
   * tokenizes as on its own.
   */
  sys_scanner_numbers_hex = sys_scanner_numbers | (1 << 13),

  /**
   * Like sys_scanner_numbers (includes it - no need to OR the two
   * together), but also recognizes two optional suffixes on an ordinary
   * (not octal/binary/hex) number as part of the same sys_scanner_number
   * token:
   *
   *  - a fractional part: a decimal point immediately followed by one or
   *    more decimal digits, e.g. "3.14". The '.' only commits if at
   *    least one digit follows it - "3." falls back to the number "3"
   *    followed by its own punctuation token "." (so "." used as a
   *    separator elsewhere isn't disturbed), and a leading '.' never
   *    starts a number on its own ("." isn't a sign or a digit - see
   *    sys_scanner_numbers).
   *  - an exponent: 'e' or 'E', an optional '+'/'-' sign, then one or
   *    more decimal digits, e.g. "1e10" or "1.5e-3" - valid with or
   *    without a fractional part first. Same "commit only what's
   *    confirmed" rule: "1e" or "1e+" (nothing valid, or a sign with no
   *    digit after it) falls back to the number "1" followed by
   *    whatever "e"/"e+" tokenizes as on their own.
   *
   * Both suffixes are checked in this order (fraction, then exponent)
   * regardless of whether the other matched. No hex/octal/binary floats
   * or exponents yet.
   */
  sys_scanner_numbers_float = sys_scanner_numbers | (1 << 14),
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
