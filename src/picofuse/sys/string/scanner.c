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
  if (r == '_' && (flags & sys_scanner_keyword_withunderscores) ==
                      sys_scanner_keyword_withunderscores) {
    return true;
  }
  if (r == '-' && (flags & sys_scanner_keyword_withdash) ==
                       sys_scanner_keyword_withdash) {
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
      bytes += next_width;
      runes++;
    }

    if (isa == sys_scanner_space && (it->flags & sys_scanner_nospace)) {
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
