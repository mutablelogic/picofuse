#include <picofuse/sys.h>

#include "iostream.h"

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

sys_rune_tokenize_t sys_rune_tokenize_init(sys_iostream_t *stream) {
  sys_rune_tokenize_t it = {
      .stream = stream,
      .start = 0,
      .bytes = 0,
      .runes = 0,
      .isa = sys_rune_other,
  };
  return it;
}

bool sys_rune_tokenize_next(sys_rune_tokenize_t *it) {
  ptrdiff_t start = sys_iostream_seek(it->stream, 0, false);

  rune_t r;
  size_t width;
  if (!_sys_rune_decode(it->stream, &r, &width)) {
    return false; // EOF
  }
  sys_rune_class_t isa = sys_rune_isa(r);
  size_t bytes = width;
  size_t runes = 1;

  // Extend the token over further runes of the same class, without
  // consuming the first rune that breaks the run - it's given back to
  // the stream, ready to start the next token instead.
  for (;;) {
    rune_t next_r;
    size_t next_width;
    if (!_sys_rune_decode(it->stream, &next_r, &next_width)) {
      break;
    }
    if (sys_rune_isa(next_r) != isa) {
      sys_iostream_seek(it->stream, -(ptrdiff_t)next_width, false);
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

size_t sys_rune_tokenize_token(sys_rune_tokenize_t *it, char *buf,
                                size_t cap) {
  ptrdiff_t saved = sys_iostream_seek(it->stream, 0, false);
  sys_iostream_seek(it->stream, it->start, true);
  size_t want = (it->bytes < cap) ? it->bytes : cap;
  size_t got = sys_iostream_read(it->stream, buf, want);
  sys_iostream_seek(it->stream, saved, true);
  return got;
}
