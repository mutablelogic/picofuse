#include "sync.h"
#include <pico/sem.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_cond_t {
  semaphore_t sem;
  int waiters_count;
  int pending_signals;
  bool init;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_cond_t _sys_cond_pool[SYS_COND_CAPACITY];
static size_t _sys_cond_pool_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Returns true when a condition variable handle is initialized. */
static inline bool _sys_cond_valid(const sys_cond_t *cond) {
  return cond != NULL && cond->init;
}

/** @brief Initializes a condition variable handle. Returns true on success. */
static inline bool _sys_cond_init_handle(sys_cond_t *cond) {
  if (cond == NULL) {
    return false;
  }
  sem_init(&cond->sem, 0, INT16_MAX);
  cond->waiters_count = 0;
  cond->pending_signals = 0;
  return true;
}

/** @brief Deinitializes a condition variable handle. */
static inline void _sys_cond_deinit_handle(sys_cond_t *cond) {
  cond->waiters_count = 0;
  cond->pending_signals = 0;
  sem_reset(&cond->sem, 0);
}

/** @brief Reconciles waiter and signal accounting after a wait returns. */
static inline void _sys_cond_finish_wait(sys_cond_t *cond, bool signaled) {
  _sys_sync_pool_lock();
  cond->waiters_count--;

  if (signaled) {
    if (cond->pending_signals > 0) {
      cond->pending_signals--;
    }
  } else if (cond->pending_signals > cond->waiters_count) {
    if (sem_try_acquire(&cond->sem)) {
      cond->pending_signals--;
    }
  }

  _sys_sync_pool_unlock();
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Allocates and initializes a condition variable from the static pool.
 */
sys_cond_t *sys_cond_init(void) {
  _sys_sync_pool_lock();

  for (size_t offset = 0; offset < SYS_COND_CAPACITY; offset++) {
    size_t index = (_sys_cond_pool_index + offset) % SYS_COND_CAPACITY;
    sys_cond_t *cond = &_sys_cond_pool[index];
    if (cond->init) {
      continue;
    }

    cond->init = true;
    if (!_sys_cond_init_handle(cond)) {
      cond->init = false;
      _sys_sync_pool_unlock();
      return NULL;
    }

    _sys_cond_pool_index = (index + 1) % SYS_COND_CAPACITY;
    _sys_sync_pool_unlock();
    return cond;
  }

  _sys_sync_pool_unlock();
  return NULL;
}

/** @brief Deinitializes a condition variable and returns its pool slot. */
void sys_cond_deinit(sys_cond_t *cond) {
  sys_assert(_sys_cond_valid(cond));

  sys_cond_broadcast(cond);

  // Wait for every woken waiter to actually consume its permit and leave
  // (waiters_count back to 0) before resetting the semaphore below.
  // sem_release() and sem_reset() both go through the semaphore's own spin
  // lock, so without this, a waiter that hasn't yet reacquired that lock to
  // consume its permit could have it wiped out from under it by
  // _sys_cond_deinit_handle()'s sem_reset(), stranding it forever.
  uint64_t start = sys_timestamp_ms();
  while (true) {
    _sys_sync_pool_lock();
    bool drained = cond->waiters_count == 0;
    _sys_sync_pool_unlock();
    if (drained) {
      break;
    }
    sys_assert(sys_timestamp_ms() - start < 1000);
    sys_sleep_ms(1);
  }

  _sys_sync_pool_lock();
  if (!cond->init) {
    _sys_sync_pool_unlock();
    return;
  }
  _sys_cond_deinit_handle(cond);
  cond->init = false;
  _sys_sync_pool_unlock();
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Waits until the condition variable is signaled. */
bool sys_cond_wait(sys_cond_t *cond, sys_mutex_t *mutex) {
  sys_assert(_sys_cond_valid(cond));

  _sys_sync_pool_lock();
  cond->waiters_count++;
  _sys_sync_pool_unlock();

  sys_assert(sys_mutex_unlock(mutex));
  sem_acquire_blocking(&cond->sem);

  _sys_cond_finish_wait(cond, true);

  sys_assert(sys_mutex_lock(mutex));
  return true;
}

/** @brief Waits until signaled or the timeout expires. */
bool sys_cond_timedwait(sys_cond_t *cond, sys_mutex_t *mutex,
                        uint32_t timeout_ms) {
  sys_assert(_sys_cond_valid(cond));

  if (timeout_ms == 0) {
    return sys_cond_wait(cond, mutex);
  }

  _sys_sync_pool_lock();
  cond->waiters_count++;
  _sys_sync_pool_unlock();

  sys_assert(sys_mutex_unlock(mutex));
  bool signaled = sem_acquire_timeout_ms(&cond->sem, timeout_ms);

  _sys_cond_finish_wait(cond, signaled);

  sys_assert(sys_mutex_lock(mutex));
  return signaled;
}

/** @brief Wakes one waiting thread when a waiter is present. */
bool sys_cond_signal(sys_cond_t *cond) {
  sys_assert(_sys_cond_valid(cond));

  _sys_sync_pool_lock();
  bool has_waiters = cond->waiters_count > cond->pending_signals;

  if (has_waiters) {
    cond->pending_signals++;
    sem_release(&cond->sem);
  }

  _sys_sync_pool_unlock();

  return true;
}

/** @brief Wakes all currently waiting threads. */
bool sys_cond_broadcast(sys_cond_t *cond) {
  sys_assert(_sys_cond_valid(cond));

  _sys_sync_pool_lock();
  int waiters = cond->waiters_count - cond->pending_signals;
  cond->pending_signals += waiters;

  for (int index = 0; index < waiters; index++) {
    sem_release(&cond->sem);
  }

  _sys_sync_pool_unlock();

  return true;
}
