#include "iostream.h"
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_iostream_t _pool[SYS_IOSTREAM_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

sys_iostream_t *_sys_iostream_alloc(const sys_iostream_ops_t *ops) {
  for (size_t i = 0; i < SYS_IOSTREAM_CAPACITY; i++) {
    if (!_pool[i].in_use) {
      _pool[i].ops = ops;
      _pool[i].in_use = true;
      return &_pool[i];
    }
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void sys_iostream_close(sys_iostream_t *s) {
  if (s == NULL) {
    return;
  }
  if (s->ops->close != NULL) {
    s->ops->close(s);
  }
  s->in_use = false;
}

size_t sys_iostream_read(sys_iostream_t *s, char *buf, size_t n) {
  if (s == NULL || n == 0) {
    return 0;
  }
  return s->ops->read(s, buf, n);
}

size_t sys_iostream_write(sys_iostream_t *s, const char *buf, size_t n) {
  if (s == NULL || n == 0) {
    return 0;
  }
  return s->ops->write(s, buf, n);
}

ptrdiff_t sys_iostream_seek(sys_iostream_t *s, ptrdiff_t offset, bool abs) {
  if (s == NULL) {
    return -1;
  }
  return s->ops->seek(s, offset, abs);
}

bool sys_iostream_set_callback(sys_iostream_t *s,
                               sys_iostream_callback_t callback,
                               void *userdata) {
  if (s == NULL || s->ops->set_callback == NULL) {
    return false;
  }
  return s->ops->set_callback(s, callback, userdata);
}

int sys_iostream_peek(sys_iostream_t *s) {
  if (s == NULL) {
    return SYS_IOSTREAM_EOF;
  }
  char c;
  if (s->ops->read(s, &c, 1) == 0) {
    return SYS_IOSTREAM_EOF;
  }
  // Every backend must be able to undo the single-byte read it just did.
  s->ops->seek(s, -1, false);
  return (int)(uint8_t)c;
}
