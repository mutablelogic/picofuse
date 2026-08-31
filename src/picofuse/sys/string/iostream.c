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

// Reads and seeks never go past .length (the current content, same as
// the string backend's own .length bound) - only write() can move that
// boundary, and only forward, up to cap - 1 (the last byte of cap is
// permanently reserved for '\0' and never itself readable/writable
// content). The buffer is kept a valid, correctly terminated C string at
// every point: data[length] == '\0' always holds, not just once writing
// is done.

static size_t _sys_iostream_buffer_read(sys_iostream_t *s, char *buf,
                                        size_t n) {
  size_t avail = s->backend.buffer.length - s->backend.buffer.pos;
  size_t got = (n < avail) ? n : avail;
  const char *src = s->backend.buffer.data + s->backend.buffer.pos;
  for (size_t i = 0; i < got; i++) {
    buf[i] = src[i];
  }
  s->backend.buffer.pos += got;
  return got;
}

static size_t _sys_iostream_buffer_write(sys_iostream_t *s, const char *buf,
                                         size_t n) {
  size_t limit = s->backend.buffer.cap - 1;
  size_t avail = limit - s->backend.buffer.pos;
  size_t put = (n < avail) ? n : avail;
  char *dst = s->backend.buffer.data + s->backend.buffer.pos;
  for (size_t i = 0; i < put; i++) {
    dst[i] = buf[i];
  }
  s->backend.buffer.pos += put;
  if (s->backend.buffer.pos > s->backend.buffer.length) {
    s->backend.buffer.length = s->backend.buffer.pos; // grew - never shrinks
  }
  s->backend.buffer.data[s->backend.buffer.length] = '\0';
  return put;
}

static ptrdiff_t _sys_iostream_buffer_seek(sys_iostream_t *s,
                                           ptrdiff_t offset, bool abs) {
  ptrdiff_t base = abs ? 0 : (ptrdiff_t)s->backend.buffer.pos;
  ptrdiff_t target = base + offset;
  if (target < 0 || (size_t)target > s->backend.buffer.length) {
    return -1; // out of bounds - position left unchanged
  }
  s->backend.buffer.pos = (size_t)target;
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

static const sys_iostream_ops_t _buffer_ops = {
    .read = _sys_iostream_buffer_read,
    .write = _sys_iostream_buffer_write,
    .seek = _sys_iostream_buffer_seek,
    .close = NULL, // nothing to release - the caller owns the buffer
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

/** Create a read/write iostream over a caller-provided mutable buffer.
 * Returns NULL on failure. */
sys_iostream_t *sys_string_open(char *buf, size_t cap) {
  if (buf == NULL || cap == 0) {
    return NULL; // cap == 0 leaves no room even for a lone '\0'
  }
  sys_iostream_t *s = _sys_iostream_alloc(&_buffer_ops);
  if (s == NULL) {
    return NULL;
  }

  // If buf already holds a valid C string (a '\0' somewhere within cap),
  // that's the starting content. Otherwise there's no way to know where
  // real content is meant to end, so start empty.
  size_t length = 0;
  bool found = false;
  for (size_t i = 0; i < cap; i++) {
    if (buf[i] == '\0') {
      length = i;
      found = true;
      break;
    }
  }
  if (!found) {
    buf[0] = '\0';
  }

  s->backend.buffer.data = buf;
  s->backend.buffer.cap = cap;
  s->backend.buffer.length = length;
  s->backend.buffer.pos = 0;
  return s;
}
