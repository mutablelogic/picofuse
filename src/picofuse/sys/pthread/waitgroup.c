#include <limits.h>
#include <picofuse/sys.h>
#include <pthread.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_waitgroup_t {
  pthread_mutex_t pmutex;
  pthread_cond_t pcond;
  int counter;
  int waiters;
  bool handle_init;
  bool init;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static pthread_mutex_t _sys_waitgroup_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static sys_waitgroup_t _sys_waitgroup_pool[SYS_WAITGROUP_CAPACITY];
static size_t _sys_waitgroup_pool_next_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Returns true when a wait group handle is initialized. */
static inline bool _sys_waitgroup_valid(const sys_waitgroup_t *wg) {
  return wg != NULL && wg->init;
}

/** @brief Initializes the native pthread state stored in a pool slot. */
static inline bool _sys_waitgroup_init_handle(sys_waitgroup_t *wg) {
  pthread_mutexattr_t mutex_attr;
  if (pthread_mutexattr_init(&mutex_attr) != 0) {
    return false;
  }

  if (pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
    pthread_mutexattr_destroy(&mutex_attr);
    return false;
  }

  int mutex_result = pthread_mutex_init(&wg->pmutex, &mutex_attr);
  pthread_mutexattr_destroy(&mutex_attr);
  if (mutex_result != 0) {
    return false;
  }

  int cond_result = pthread_cond_init(&wg->pcond, NULL);
  if (cond_result != 0) {
    pthread_mutex_destroy(&wg->pmutex);
    return false;
  }

  wg->counter = 0;
  wg->waiters = 0;
  wg->handle_init = true;
  return true;
}

/**
 * @brief Resets a wait group's counters for return to the static pool.
 *
 * The native pthread mutex/cond stay initialized (handle_init remains true)
 * so the slot can be reused without re-creating them.
 */
static inline void _sys_waitgroup_deinit_handle(sys_waitgroup_t *wg) {
  wg->counter = 0;
  wg->waiters = 0;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Allocates and initializes a wait group from the static pool. */
sys_waitgroup_t *sys_waitgroup_init(void) {
  if (pthread_mutex_lock(&_sys_waitgroup_pool_lock) != 0) {
    return NULL;
  }

  for (size_t offset = 0; offset < SYS_WAITGROUP_CAPACITY; offset++) {
    size_t index =
        (_sys_waitgroup_pool_next_index + offset) % SYS_WAITGROUP_CAPACITY;
    sys_waitgroup_t *wg = &_sys_waitgroup_pool[index];
    if (wg->init) {
      continue;
    }

    if (!wg->handle_init && !_sys_waitgroup_init_handle(wg)) {
      pthread_mutex_unlock(&_sys_waitgroup_pool_lock);
      return NULL;
    }

    wg->counter = 0;
    wg->waiters = 0;
    wg->init = true;

    _sys_waitgroup_pool_next_index = (index + 1) % SYS_WAITGROUP_CAPACITY;
    pthread_mutex_unlock(&_sys_waitgroup_pool_lock);
    return wg;
  }

  pthread_mutex_unlock(&_sys_waitgroup_pool_lock);
  return NULL;
}

/** @brief Deinitializes a wait group and returns its pool slot. */
void sys_waitgroup_deinit(sys_waitgroup_t *wg) {
  sys_assert(_sys_waitgroup_valid(wg));

  // Release any straggling waiters before tearing down, mirroring
  // sys_cond_deinit's safety broadcast. Safe to do unconditionally here:
  // the native pthread mutex/cond are never destroyed or reset by deinit
  // (they're kept alive for pool reuse), so there's nothing for a woken
  // waiter to race against afterward.
  if (pthread_mutex_lock(&wg->pmutex) == 0) {
    wg->counter = 0;
    pthread_cond_broadcast(&wg->pcond);
    pthread_mutex_unlock(&wg->pmutex);
  }

  int lock_result = pthread_mutex_lock(&_sys_waitgroup_pool_lock);
  if (lock_result != 0) {
    return;
  }

  if (!wg->init) {
    pthread_mutex_unlock(&_sys_waitgroup_pool_lock);
    return;
  }

  _sys_waitgroup_deinit_handle(wg);
  wg->init = false;

  pthread_mutex_unlock(&_sys_waitgroup_pool_lock);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Adds delta to the wait group counter. */
bool sys_waitgroup_add(sys_waitgroup_t *wg, int delta) {
  sys_assert(_sys_waitgroup_valid(wg));

  if (delta < 0) {
    return false;
  }

  int lock_result = pthread_mutex_lock(&wg->pmutex);
  if (lock_result != 0) {
    return false;
  }

  if (!wg->init) {
    pthread_mutex_unlock(&wg->pmutex);
    return false;
  }

  bool ok = true;
  if (wg->counter > INT_MAX - delta) {
    ok = false;
  } else {
    wg->counter += delta;
  }

  int unlock_result = pthread_mutex_unlock(&wg->pmutex);
  return ok && unlock_result == 0;
}

/** @brief Decrements the wait group counter by one. */
bool sys_waitgroup_done(sys_waitgroup_t *wg) {
  sys_assert(_sys_waitgroup_valid(wg));

  int lock_result = pthread_mutex_lock(&wg->pmutex);
  if (lock_result != 0) {
    return false;
  }

  if (!wg->init) {
    pthread_mutex_unlock(&wg->pmutex);
    return false;
  }

  bool ok = true;
  if (wg->counter <= 0) {
    ok = false;
  } else {
    wg->counter--;
    if (wg->counter == 0) {
      ok = pthread_cond_broadcast(&wg->pcond) == 0;
    }
  }

  int unlock_result = pthread_mutex_unlock(&wg->pmutex);
  return ok && unlock_result == 0;
}

/** @brief Waits for the counter to reach zero. */
void sys_waitgroup_wait(sys_waitgroup_t *wg) {
  sys_assert(_sys_waitgroup_valid(wg));

  int lock_result = pthread_mutex_lock(&wg->pmutex);
  sys_assert(lock_result == 0);
  if (lock_result != 0) {
    return;
  }

  if (!wg->init) {
    pthread_mutex_unlock(&wg->pmutex);
    return;
  }

  wg->waiters++;

  while (wg->counter > 0) {
    int wait_result = pthread_cond_wait(&wg->pcond, &wg->pmutex);
    sys_assert(wait_result == 0);
    if (wait_result != 0) {
      break;
    }
  }

  wg->waiters--;

  int unlock_result = pthread_mutex_unlock(&wg->pmutex);
  sys_assert(unlock_result == 0);
}
