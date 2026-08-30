#include <picofuse/sys.h>

// Stub: a single-threaded pool that tracks allocation and lock state
// faithfully (including the SYS_MUTEX_CAPACITY exhaustion contract) without
// providing real cross-thread blocking, since this platform has no thread
// primitives to block on.

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_mutex_t {
  bool used;
  bool locked;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct sys_mutex_t _sys_mutex_pool[SYS_MUTEX_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

sys_mutex_t *sys_mutex_init(void) {
  for (size_t i = 0; i < SYS_MUTEX_CAPACITY; i++) {
    sys_mutex_t *mutex = &_sys_mutex_pool[i];
    if (!mutex->used) {
      mutex->used = true;
      mutex->locked = false;
      return mutex;
    }
  }
  return NULL;
}

void sys_mutex_deinit(sys_mutex_t *mutex) {
  mutex->used = false;
  mutex->locked = false;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

bool sys_mutex_lock(sys_mutex_t *mutex) {
  mutex->locked = true;
  return true;
}

bool sys_mutex_trylock(sys_mutex_t *mutex) {
  if (mutex->locked) {
    return false;
  }
  mutex->locked = true;
  return true;
}

bool sys_mutex_unlock(sys_mutex_t *mutex) {
  mutex->locked = false;
  return true;
}
