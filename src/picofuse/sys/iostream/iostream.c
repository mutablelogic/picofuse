#include "iostream.h"
#include <stdint.h>

// Same shared-pool-lock convention as hid/private.h and hw/led/private.h -
// this pool is claimed/released from more than one thread in real use
// (a POSIX net listener's own accept thread allocates a stream for each
// new connection, concurrently with the application closing others), and
// was previously entirely unsynchronized: two threads could both see a
// slot as free and claim it, corrupting whichever stream lost the race
// (or silently sharing one connection's backing state with another's).
#ifdef SYSTEM_NAME_PICO
#include "../pico/sync.h"
#define _SYS_IOSTREAM_LOCK() _sys_sync_pool_lock()
#define _SYS_IOSTREAM_UNLOCK() _sys_sync_pool_unlock()
#else
#include <pthread.h>
static pthread_mutex_t _sys_iostream_lock = PTHREAD_MUTEX_INITIALIZER;
#define _SYS_IOSTREAM_LOCK() pthread_mutex_lock(&_sys_iostream_lock)
#define _SYS_IOSTREAM_UNLOCK() pthread_mutex_unlock(&_sys_iostream_lock)
#endif

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_iostream_t _pool[SYS_IOSTREAM_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

sys_iostream_t *_sys_iostream_alloc(const sys_iostream_ops_t *ops) {
  _SYS_IOSTREAM_LOCK();
  sys_iostream_t *slot = NULL;
  for (size_t i = 0; i < SYS_IOSTREAM_CAPACITY; i++) {
    if (!_pool[i].in_use) {
      _pool[i].ops = ops;
      _pool[i].in_use = true;
      slot = &_pool[i];
      break;
    }
  }
  _SYS_IOSTREAM_UNLOCK();
  return slot;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void sys_iostream_close(sys_iostream_t *s) {
  if (s == NULL) {
    return;
  }
  // ops->close() runs outside the lock - a backend's own close (e.g. a
  // POSIX net/uart stream's) can block for a while joining its own
  // background thread (see TODO.md's note on that having its own
  // separate, unrelated deadlock risk), and holding this pool-wide lock
  // for that long would stall every other stream's alloc/close too.
  if (s->ops->close != NULL) {
    s->ops->close(s);
  }
  _SYS_IOSTREAM_LOCK();
  s->in_use = false;
  _SYS_IOSTREAM_UNLOCK();
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

bool sys_iostream_eof(sys_iostream_t *s) {
  if (s == NULL || s->ops->eof == NULL) {
    return false;
  }
  return s->ops->eof(s);
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
