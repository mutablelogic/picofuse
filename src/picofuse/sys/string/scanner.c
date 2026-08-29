#include <picofuse/sys.h>

#include "iostream.h"

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool is_keyword_start(rune_t r) {
  return (r >= 'A' && r <= 'Z') || (r >= 'a' && r <= 'z');
}

// Whether r can extend an already-started sys_scanner_keyword run: any
// keyword-starting letter, a digit, or (only if the caller opted in) an
// underscore or dash.
static bool is_keyword_continue(rune_t r, sys_scanner_flags_t flags) {
  if (is_keyword_start(r) || (r >= '0' && r <= '9')) {
    return true;
  }
  if (r == '_' && (flags & sys_scanner_keywords_withunderscores) ==
                      sys_scanner_keywords_withunderscores) {
    return true;
  }
  if (r == '-' && (flags & sys_scanner_keywords_withdashes) ==
                       sys_scanner_keywords_withdashes) {
    return true;
  }
  return false;
}

static sys_scanner_class_t classify(rune_t r, sys_scanner_flags_t flags) {
  if ((flags & sys_scanner_newlines) && r == '\n') {
    return sys_scanner_newline;
  }
  if ((flags & sys_scanner_keywords) && is_keyword_start(r)) {
    return sys_scanner_keyword;
  }
  switch (sys_rune_isa(r)) {
  case sys_rune_space:
    return sys_scanner_space;
  case sys_rune_digit:
    return sys_scanner_digit;
  case sys_rune_alpha:
    return sys_scanner_alpha;
  case sys_rune_punct:
    return sys_scanner_punct;
  case sys_rune_symbol:
    return sys_scanner_symbol;
  case sys_rune_control:
    return sys_scanner_control;
  case sys_rune_other:
  default:
    return sys_scanner_other;
  }
}

static bool ishex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

// stream is positioned right after a confirmed leading backslash.
// Returns true and leaves the stream positioned right after the
// recognized escape sequence if one was found; otherwise returns false
// and restores the stream to exactly where it was when called (right
// after the backslash), so the caller can fall back to ordinary
// classification.
static bool match_escape(sys_iostream_t *stream) {
  ptrdiff_t after_backslash = sys_iostream_seek(stream, 0, false);

  char c;
  if (sys_iostream_read(stream, &c, 1) == 0) {
    return false; // nothing follows the backslash
  }
  switch (c) {
  case '"':
  case '\\':
  case '/':
  case 'b':
  case 'f':
  case 'n':
  case 'r':
  case 't':
    return true;
  case 'u': {
    char hex[4];
    size_t got = sys_iostream_read(stream, hex, 4);
    if (got == 4 && ishex(hex[0]) && ishex(hex[1]) && ishex(hex[2]) &&
        ishex(hex[3])) {
      return true;
    }
    sys_iostream_seek(stream, after_backslash, true);
    return false;
  }
  default:
    sys_iostream_seek(stream, after_backslash, true);
    return false;
  }
}

// Whether r is a quote character whose matching flag is set - the start
// (or, from within a run, the interrupt point) of a quoted string.
static bool is_quote_start(rune_t r, sys_scanner_flags_t flags) {
  if ((flags & sys_scanner_quotes_single) && r == '\'') {
    return true;
  }
  if ((flags & sys_scanner_quotes_double) && r == '"') {
    return true;
  }
  return false;
}

// stream is positioned right after a confirmed opening quote rune (the
// same rune passed as `quote`). Consumes through the closing (unescaped)
// occurrence of quote, or through end-of-stream if none is found (a
// best-effort "unterminated string" - the caller can tell the two apart
// by checking whether the token's last byte is quote). A backslash
// escapes whatever single rune follows it - that rune never ends the
// string, whatever it is (not just the quote character itself - see
// sys_scanner_quotes_single's doc comment for why '\\' needs the same
// treatment). Escape sequences are left byte-for-byte in the stream;
// unescaping/unquoting is a separate, later step. Returns the number of
// runes consumed, not counting the opening quote but counting the
// closing one if found.
static size_t scan_quoted(sys_iostream_t *stream, rune_t quote) {
  size_t runes = 0;
  for (;;) {
    rune_t r;
    size_t width;
    if (!_sys_rune_decode(stream, &r, &width)) {
      return runes; // unterminated - stream exhausted
    }
    runes++;
    if (r == quote) {
      return runes; // closing quote found
    }
    if (r == '\\') {
      rune_t escaped;
      size_t escaped_width;
      if (_sys_rune_decode(stream, &escaped, &escaped_width)) {
        runes++;
      }
      // If decode fails here (stream ends right after the backslash),
      // there's nothing left to skip - the string is just unterminated.
    }
  }
}

// stream is positioned right after a confirmed leading '/'. Returns true
// and leaves the stream positioned right after the second '/' if this
// is really a line comment; otherwise returns false and restores the
// stream to exactly where it was when called (right after the first
// '/'), so the caller can fall back to ordinary classification.
static bool match_slash_comment(sys_iostream_t *stream) {
  ptrdiff_t after_first_slash = sys_iostream_seek(stream, 0, false);
  char c;
  if (sys_iostream_read(stream, &c, 1) == 1 && c == '/') {
    return true;
  }
  sys_iostream_seek(stream, after_first_slash, true);
  return false;
}

// stream is positioned wherever a comment's content starts (right after
// whatever prefix already identified it as one - e.g. the leading '#').
// Consumes through (but not including) the next '\n', or through
// end-of-stream if none is found - a comment runs to end of line or end
// of stream, whichever comes first. The newline itself is left on the
// stream for the next sys_scanner_next() call to classify normally, not
// treated as part of the comment. Returns the number of runes consumed.
static size_t scan_to_eol(sys_iostream_t *stream) {
  size_t runes = 0;
  for (;;) {
    ptrdiff_t before = sys_iostream_seek(stream, 0, false);
    rune_t r;
    size_t width;
    if (!_sys_rune_decode(stream, &r, &width)) {
      return runes; // end of stream
    }
    if (r == '\n') {
      sys_iostream_seek(stream, before, true); // give back the newline
      return runes;
    }
    runes++;
  }
}

static bool is_number_start(rune_t r) {
  return r == '-' || r == '+' || (r >= '0' && r <= '9');
}

static bool isdecimal(char c) { return c >= '0' && c <= '9'; }
static bool isoctaldigit(char c) { return c >= '0' && c <= '7'; }
static bool isbinarydigit(char c) { return c == '0' || c == '1'; }

// stream is positioned right after some already-confirmed prefix.
// Consumes a maximal run of bytes satisfying is_digit, giving back the
// first non-matching byte (or stopping cleanly at end of stream) -
// digits are always single-byte ASCII, so byte count and rune count
// coincide here. Returns the number of digits consumed (possibly 0).
static size_t scan_digit_run(sys_iostream_t *stream, bool (*is_digit)(char)) {
  size_t n = 0;
  for (;;) {
    ptrdiff_t before = sys_iostream_seek(stream, 0, false);
    char c;
    if (sys_iostream_read(stream, &c, 1) != 1 || !is_digit(c)) {
      sys_iostream_seek(stream, before, true);
      break;
    }
    n++;
  }
  return n;
}

// stream is positioned right after a confirmed leading '0' (not counted
// here - the caller already counted it). Recognizes prefix_lower or
// prefix_upper (e.g. 'x'/'X') followed by at least one digit satisfying
// is_digit; if found, consumes the prefix byte plus the whole digit run
// and returns true with *out_runes = 1 (the prefix byte) + digit run
// length. If not found - no prefix byte, or a prefix byte with no valid
// digit after it - restores the stream to exactly where it was called
// (right after the '0') and returns false, so the caller can fall back
// to treating the '0' as an ordinary decimal number.
static bool scan_prefixed_digits(sys_iostream_t *stream, char prefix_lower,
                                  char prefix_upper, bool (*is_digit)(char),
                                  size_t *out_runes) {
  ptrdiff_t before = sys_iostream_seek(stream, 0, false);
  char c;
  if (sys_iostream_read(stream, &c, 1) != 1 ||
      !(c == prefix_lower || c == prefix_upper)) {
    sys_iostream_seek(stream, before, true);
    return false;
  }
  size_t digit_runes = scan_digit_run(stream, is_digit);
  if (digit_runes == 0) {
    sys_iostream_seek(stream, before, true); // no digits - not a prefixed number
    return false;
  }
  *out_runes = 1 + digit_runes;
  return true;
}

// stream is positioned right after a confirmed integer digit run.
// Recognizes a fractional suffix: a '.' immediately followed by at least
// one decimal digit. If found, consumes it and returns true with
// *out_runes = 1 (the '.') + digit run length; otherwise restores the
// stream to exactly where it was called and returns false, so a bare
// trailing '.' (nothing after it, or a non-digit) is left for its own
// token instead of being swallowed.
static bool scan_fraction(sys_iostream_t *stream, size_t *out_runes) {
  ptrdiff_t before = sys_iostream_seek(stream, 0, false);
  char c;
  if (sys_iostream_read(stream, &c, 1) != 1 || c != '.') {
    sys_iostream_seek(stream, before, true);
    return false;
  }
  size_t digit_runes = scan_digit_run(stream, isdecimal);
  if (digit_runes == 0) {
    sys_iostream_seek(stream, before, true); // '.' not followed by a digit
    return false;
  }
  *out_runes = 1 + digit_runes;
  return true;
}

// stream is positioned right after a confirmed integer digit run (and
// possibly a fractional suffix already consumed by scan_fraction).
// Recognizes an exponent suffix: 'e' or 'E', an optional '+'/'-' sign,
// then one or more decimal digits. If found, consumes it and returns
// true with *out_runes = 1 (the 'e'/'E') + (1 if a sign was present) +
// digit run length; otherwise restores the stream to exactly where it
// was called and returns false, so "1e" (nothing valid after it) or
// "1e+" (a sign but no digit after it) leaves 'e'/'e+' for their own
// tokens instead of being swallowed.
static bool scan_exponent(sys_iostream_t *stream, size_t *out_runes) {
  ptrdiff_t before = sys_iostream_seek(stream, 0, false);
  char c;
  if (sys_iostream_read(stream, &c, 1) != 1 || !(c == 'e' || c == 'E')) {
    sys_iostream_seek(stream, before, true);
    return false;
  }
  size_t runes = 1;

  ptrdiff_t before_sign = sys_iostream_seek(stream, 0, false);
  char sign;
  if (sys_iostream_read(stream, &sign, 1) == 1 && (sign == '+' || sign == '-')) {
    runes++;
  } else {
    sys_iostream_seek(stream, before_sign, true); // give back what we peeked
  }

  size_t digit_runes = scan_digit_run(stream, isdecimal);
  if (digit_runes == 0) {
    sys_iostream_seek(stream, before, true); // no digits - not an exponent
    return false;
  }
  runes += digit_runes;
  *out_runes = runes;
  return true;
}

// r is the rune that triggered this check (a sign or a digit); stream is
// positioned right after it. If r is a digit, this always succeeds - r
// counts as the first digit, and any further digits (plus, per flags, an
// octal/binary/hex prefix or a float's fractional suffix) are consumed
// too. If r is a sign, at least one digit must follow immediately, or
// this isn't a number at all: returns false and restores the stream to
// right after the sign, so the caller can fall back to ordinary
// punct/symbol classification of the sign alone. *out_runes receives the
// number of runes consumed beyond r itself.
static bool scan_number(sys_iostream_t *stream, rune_t r,
                         sys_scanner_flags_t flags, size_t *out_runes) {
  size_t runes = 0;

  if (r == '-' || r == '+') {
    ptrdiff_t before = sys_iostream_seek(stream, 0, false);
    rune_t next_r;
    size_t next_width;
    if (!_sys_rune_decode(stream, &next_r, &next_width) ||
        !(next_r >= '0' && next_r <= '9')) {
      sys_iostream_seek(stream, before, true);
      *out_runes = 0;
      return false; // a lone sign isn't a number
    }
    runes++;
    r = next_r; // r is now the first actual digit, for prefix detection below
  }

  if (r == '0') {
    if ((flags & sys_scanner_numbers_hex) == sys_scanner_numbers_hex) {
      size_t prefixed_runes;
      if (scan_prefixed_digits(stream, 'x', 'X', ishex, &prefixed_runes)) {
        *out_runes = runes + prefixed_runes;
        return true;
      }
    }
    if ((flags & sys_scanner_numbers_binary) == sys_scanner_numbers_binary) {
      size_t prefixed_runes;
      if (scan_prefixed_digits(stream, 'b', 'B', isbinarydigit,
                                &prefixed_runes)) {
        *out_runes = runes + prefixed_runes;
        return true;
      }
    }
    if ((flags & sys_scanner_numbers_octal) == sys_scanner_numbers_octal) {
      // Prefixed form first ("0o"/"0O", self-describing like hex/binary
      // - see scan_prefixed_digits) - then the bare C-style fallback (a
      // leading zero directly followed by octal digits, no marker).
      size_t prefixed_runes;
      if (scan_prefixed_digits(stream, 'o', 'O', isoctaldigit,
                                &prefixed_runes)) {
        *out_runes = runes + prefixed_runes;
        return true;
      }
      size_t octal_runes = scan_digit_run(stream, isoctaldigit);
      if (octal_runes > 0) {
        *out_runes = runes + octal_runes;
        return true;
      }
    }
  }

  runes += scan_digit_run(stream, isdecimal);

  if ((flags & sys_scanner_numbers_float) == sys_scanner_numbers_float) {
    size_t frac_runes;
    if (scan_fraction(stream, &frac_runes)) {
      runes += frac_runes;
    }
    size_t exp_runes;
    if (scan_exponent(stream, &exp_runes)) {
      runes += exp_runes;
    }
  }

  *out_runes = runes;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

sys_scanner_t sys_scanner_init(sys_iostream_t *stream,
                                sys_scanner_flags_t flags) {
  sys_scanner_t it = {
      .stream = stream,
      .flags = flags,
      .start = 0,
      .bytes = 0,
      .runes = 0,
      .isa = sys_scanner_other,
  };
  return it;
}

bool sys_scanner_next(sys_scanner_t *it) {
  ptrdiff_t start = sys_iostream_seek(it->stream, 0, false);

  rune_t r;
  size_t width;
  if (!_sys_rune_decode(it->stream, &r, &width)) {
    return false; // EOF
  }

  if ((it->flags & sys_scanner_escapes) && r == '\\') {
    if (match_escape(it->stream)) {
      ptrdiff_t end = sys_iostream_seek(it->stream, 0, false);
      it->start = start;
      it->bytes = (size_t)(end - start);
      it->runes = it->bytes; // every escape-sequence byte is ASCII
      it->isa = sys_scanner_escape;
      return true;
    }
    // Not a recognized escape - fall through and tokenize '\' as
    // ordinary punctuation instead. match_escape() already restored
    // the stream to right after the backslash.
  }

  if (is_quote_start(r, it->flags)) {
    size_t inner_runes = scan_quoted(it->stream, r);
    ptrdiff_t end = sys_iostream_seek(it->stream, 0, false);
    it->start = start;
    it->bytes = (size_t)(end - start);
    it->runes = 1 + inner_runes; // opening quote + whatever scan_quoted consumed
    it->isa = sys_scanner_string;
    return true;
  }

  if ((it->flags & sys_scanner_comments_hash) && r == '#') {
    size_t inner_runes = scan_to_eol(it->stream);
    ptrdiff_t end = sys_iostream_seek(it->stream, 0, false);
    it->start = start;
    it->bytes = (size_t)(end - start);
    it->runes = 1 + inner_runes; // leading '#' + whatever scan_to_eol consumed
    it->isa = sys_scanner_comment;
    return true;
  }

  if ((it->flags & sys_scanner_comments_slash) && r == '/') {
    if (match_slash_comment(it->stream)) {
      size_t inner_runes = scan_to_eol(it->stream);
      ptrdiff_t end = sys_iostream_seek(it->stream, 0, false);
      it->start = start;
      it->bytes = (size_t)(end - start);
      it->runes = 2 + inner_runes; // both '/'s + whatever scan_to_eol consumed
      it->isa = sys_scanner_comment;
      return true;
    }
    // Not a recognized comment start - fall through and tokenize '/'
    // as ordinary punctuation instead. match_slash_comment() already
    // restored the stream to right after the first '/'.
  }

  if ((it->flags & sys_scanner_numbers) && is_number_start(r)) {
    size_t inner_runes;
    if (scan_number(it->stream, r, it->flags, &inner_runes)) {
      ptrdiff_t end = sys_iostream_seek(it->stream, 0, false);
      it->start = start;
      it->bytes = (size_t)(end - start);
      it->runes = 1 + inner_runes; // r itself + whatever scan_number consumed
      it->isa = sys_scanner_number;
      return true;
    }
    // A lone sign with no digit after it - fall through and tokenize
    // it as ordinary punctuation/symbol instead. scan_number() already
    // restored the stream to right after the sign.
  }

  sys_scanner_class_t isa = classify(r, it->flags);
  size_t bytes = width;
  size_t runes = 1;

  // Punctuation, symbols, newlines and control characters are always
  // one rune per token, never a run - each is a single, individually
  // meaningful character (a structural delimiter like '{'/','/'/'/',
  // or something a caller wants to count one at a time, like
  // sys_scanner_newline). Every other class still extends over a
  // maximal run of the same class below.
  bool single_rune = isa == sys_scanner_punct || isa == sys_scanner_symbol ||
                      isa == sys_scanner_newline || isa == sys_scanner_control;

  // Extend the token over further runes of the same class, without
  // consuming the first rune that breaks the run - it's given back to
  // the stream, ready to start the next token instead.
  while (!single_rune) {
    ptrdiff_t before_next = sys_iostream_seek(it->stream, 0, false);
    rune_t next_r;
    size_t next_width;
    if (!_sys_rune_decode(it->stream, &next_r, &next_width)) {
      break;
    }
    // A keyword run mixes classes by design (letters, digits, and
    // optionally '_'/'-'), so it can't be extended by a plain
    // classify()-equality check the way every other run can.
    bool same_run = (isa == sys_scanner_keyword)
                         ? is_keyword_continue(next_r, it->flags)
                         : (classify(next_r, it->flags) == isa);
    if (!same_run) {
      sys_iostream_seek(it->stream, before_next, true);
      break;
    }
    bytes += next_width;
    runes++;
  }

  it->start = start;
  it->bytes = bytes;
  it->runes = runes;
  it->isa = isa;
  return true;
}

size_t sys_scanner_token(sys_scanner_t *it, char *buf, size_t cap) {
  ptrdiff_t saved = sys_iostream_seek(it->stream, 0, false);
  sys_iostream_seek(it->stream, it->start, true);
  size_t want = (it->bytes < cap) ? it->bytes : cap;
  size_t got = sys_iostream_read(it->stream, buf, want);
  sys_iostream_seek(it->stream, saved, true);
  return got;
}
