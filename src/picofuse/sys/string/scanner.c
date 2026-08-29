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
  if ((flags & sys_scanner_squotes) && r == '\'') {
    return true;
  }
  if ((flags & sys_scanner_dquotes) && r == '"') {
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
// sys_scanner_squotes's doc comment for why '\\' needs the same
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
  for (;;) {
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

    sys_scanner_class_t isa = classify(r, it->flags);
    size_t bytes = width;
    size_t runes = 1;

    // Extend the token over further runes of the same class, without
    // consuming the first rune that breaks the run - it's given back to
    // the stream, ready to start the next token (or the next skipped
    // run) instead.
    for (;;) {
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
      // Don't merge a '\' that starts a recognized escape into this
      // punctuation run - it needs to start its own token.
      if ((it->flags & sys_scanner_escapes) && next_r == '\\' &&
          match_escape(it->stream)) {
        sys_iostream_seek(it->stream, before_next, true);
        break;
      }
      // Don't merge a quote that starts a string into this punctuation
      // run either - it needs to start its own token.
      if (is_quote_start(next_r, it->flags)) {
        sys_iostream_seek(it->stream, before_next, true);
        break;
      }
      // Nor a '#' that starts a comment.
      if ((it->flags & sys_scanner_comments_hash) && next_r == '#') {
        sys_iostream_seek(it->stream, before_next, true);
        break;
      }
      // Nor a '/' that starts a recognized // comment.
      if ((it->flags & sys_scanner_comments_slash) && next_r == '/' &&
          match_slash_comment(it->stream)) {
        sys_iostream_seek(it->stream, before_next, true);
        break;
      }
      bytes += next_width;
      runes++;
    }

    if (isa == sys_scanner_space && (it->flags & sys_scanner_nospaces)) {
      continue; // skip this run entirely, scan for the next token
    }

    it->start = start;
    it->bytes = bytes;
    it->runes = runes;
    it->isa = isa;
    return true;
  }
}

size_t sys_scanner_token(sys_scanner_t *it, char *buf, size_t cap) {
  ptrdiff_t saved = sys_iostream_seek(it->stream, 0, false);
  sys_iostream_seek(it->stream, it->start, true);
  size_t want = (it->bytes < cap) ? it->bytes : cap;
  size_t got = sys_iostream_read(it->stream, buf, want);
  sys_iostream_seek(it->stream, saved, true);
  return got;
}
