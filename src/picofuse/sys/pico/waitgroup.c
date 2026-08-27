#include "sync.h"
#include <limits.h>
#include <pico/mutex.h>
#include <pico/sem.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_waitgroup_t {
  semaphore_t sem;
  mutex_t lock;
  int counter;
  int waiters;
  bool init;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_waitgroup_t _sys_waitgroup_pool[SYS_WAITGROUP_CAPACITY];
static size_t _sys_waitgroup_pool_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Returns true when a wait group handle is initialized. */
static inline bool _sys_waitgroup_valid(const sys_waitgroup_t *wg) {
  return wg != NULL && wg->init && mutex_is_initialized((mutex_t *)&wg->lock);
}

/** @brief Initializes the native Pico primitives stored in a pool slot. */
static inline bool _sys_waitgroup_init_handle(sys_waitgroup_t *wg) {
  sem_init(&wg->sem, 0, INT16_MAX);
  mutex_init(&wg->lock);
  wg->counter = 0;
  wg->waiters = 0;
  return mutex_is_initialized(&wg->lock);
}

/** @brief Releases a wait group slot back to the static pool. */
static inline void _sys_waitgroup_deinit_handle(sys_waitgroup_t *wg) {
  wg->counter = 0;
  wg->waiters = 0;
  sem_reset(&wg->sem, 0);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Allocates and initializes a wait group from the static pool. */
sys_waitgroup_t *sys_waitgroup_init(void) {
  _sys_sync_pool_lock();

  for (size_t offset = 0; offset < SYS_WAITGROUP_CAPACITY; offset++) {
    size_t index =
        (_sys_waitgroup_pool_index + offset) % SYS_WAITGROUP_CAPACITY;
    sys_waitgroup_t *wg = &_sys_waitgroup_pool[index];
    if (wg->init) {
      continue;
    }

    wg->init = true;
    if (!_sys_waitgroup_init_handle(wg)) {
      wg->init = false;
      _sys_sync_pool_unlock();
      return NULL;
    }

    _sys_waitgroup_pool_index = (index + 1) % SYS_WAITGROUP_CAPACITY;
    _sys_sync_pool_unlock();
    return wg;
  }

  _sys_sync_pool_unlock();
  return NULL;
}

/** @brief Deinitializes a wait group and returns its pool slot. */
void sys_waitgroup_deinit(sys_waitgroup_t *wg) {
  sys_assert(_sys_waitgroup_valid(wg));

  // Release any straggling waiters before tearing down, mirroring
  // sys_cond_deinit's safety broadcast: force the counter to zero and wake
  // everyone currently blocked in sys_waitgroup_wait().
  mutex_enter_blocking(&wg->lock);
  if (wg->init) {
    wg->counter = 0;
    for (int index = 0; index < wg->waiters; index++) {
      sem_release(&wg->sem);
    }
  }
  mutex_exit(&wg->lock);

  // Wait for every woken waiter to actually consume its permit and leave
  // (waiters back to 0) before resetting the semaphore below.
  // sem_release() and sem_reset() both go through the semaphore's own spin
  // lock, so without this, a waiter that hasn't yet reacquired that lock to
  // consume its permit could have it wiped out from under it by
  // _sys_waitgroup_deinit_handle()'s sem_reset(), stranding it forever.
  uint64_t start = sys_timestamp_ms();
  while (true) {
    mutex_enter_blocking(&wg->lock);
    bool drained = wg->waiters == 0;
    mutex_exit(&wg->lock);
    if (drained) {
      break;
    }
    sys_assert(sys_timestamp_ms() - start < 1000);
    sys_sleep_ms(1);
  }

  _sys_sync_pool_lock();
  if (!wg->init) {
    _sys_sync_pool_unlock();
    return;
  }
  _sys_waitgroup_deinit_handle(wg);
  wg->init = false;
  _sys_sync_pool_unlock();
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Adds delta to the wait group counter. */
bool sys_waitgroup_add(sys_waitgroup_t *wg, int delta) {
  sys_assert(_sys_waitgroup_valid(wg));

  if (delta < 0) {
    return false;
  }

  mutex_enter_blocking(&wg->lock);

  if (!wg->init) {
    mutex_exit(&wg->lock);
    return false;
  }

  bool ok = true;
  if (wg->counter > INT_MAX - delta) {
    ok = false;
  } else {
    wg->counter += delta;
  }

  mutex_exit(&wg->lock);
  return ok;
}

/** @brief Decrements the wait group counter by one. */
bool sys_waitgroup_done(sys_waitgroup_t *wg) {
  sys_assert(_sys_waitgroup_valid(wg));

  mutex_enter_blocking(&wg->lock);

  if (!wg->init) {
    mutex_exit(&wg->lock);
    return false;
  }

  bool ok = true;
  if (wg->counter <= 0) {
    ok = false;
  } else {
    wg->counter--;
    if (wg->counter == 0) {
      for (int index = 0; index < wg->waiters; index++) {
        sem_release(&wg->sem);
      }
    }
  }

  mutex_exit(&wg->lock);
  return ok;
}

/** @brief Waits for the counter to reach zero. */
void sys_waitgroup_wait(sys_waitgroup_t *wg) {
  sys_assert(_sys_waitgroup_valid(wg));

  mutex_enter_blocking(&wg->lock);
  if (!wg->init) {
    mutex_exit(&wg->lock);
    return;
  }

  wg->waiters++;
  bool should_block = wg->counter > 0;
  mutex_exit(&wg->lock);

  if (should_block) {
    sem_acquire_blocking(&wg->sem);
  }

  mutex_enter_blocking(&wg->lock);
  wg->waiters--;
  mutex_exit(&wg->lock);
}
