#include <picofuse/sys.h>

#include "../iostream/iostream.h"
#include "iostream.h"

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

bool _sys_rune_decode(sys_iostream_t *stream, rune_t *r, size_t *width) {
  char raw[5] = {0};
  size_t got = sys_iostream_read(stream, raw, 4);
  if (got == 0) {
    return false; // EOF
  }
  const char *end = sys_rune_next(raw, r);
  size_t consumed = (size_t)(end - raw);
  if (consumed < got) {
    sys_iostream_seek(stream, -(ptrdiff_t)(got - consumed), false);
  }
  *width = consumed;
  return true;
}

static size_t _sys_iostream_read(sys_iostream_t *s, char *buf, size_t n) {
  size_t avail = s->backend.string.length - s->backend.string.pos;
  size_t got = (n < avail) ? n : avail;
  const char *src = s->backend.string.start + s->backend.string.pos;
  for (size_t i = 0; i < got; i++) {
    buf[i] = src[i];
  }
  s->backend.string.pos += got;
  return got;
}

static size_t _sys_iostream_write(sys_iostream_t *s, const char *buf,
                                  size_t n) {
  (void)s;
  (void)buf;
  (void)n;
  return 0; // read-only
}

static ptrdiff_t _sys_iostream_seek(sys_iostream_t *s, ptrdiff_t offset,
                                    bool abs) {
  ptrdiff_t base = abs ? 0 : (ptrdiff_t)s->backend.string.pos;
  ptrdiff_t target = base + offset;
  if (target < 0 || (size_t)target > s->backend.string.length) {
    return -1; // out of bounds - position left unchanged
  }
  s->backend.string.pos = (size_t)target;
  return target;
}

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static const sys_iostream_ops_t _ops = {
    .read = _sys_iostream_read,
    .write = _sys_iostream_write,
    .seek = _sys_iostream_seek,
    .close = NULL, // nothing to release beyond the pool slot itself
};

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** Create a read-only iostream from a null-terminated string. Returns NULL on
 * failure. */
sys_iostream_t *sys_string_read(const char *str) {
  if (str == NULL) {
    return NULL;
  }
  sys_iostream_t *s = _sys_iostream_alloc(&_ops);
  if (s == NULL) {
    return NULL;
  }
  s->backend.string.start = str;
  s->backend.string.length = sys_string_bytes(str);
  s->backend.string.pos = 0;
  return s;
}
