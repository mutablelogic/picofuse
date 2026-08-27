#include "sync.h"
#include <pico/mutex.h>
#include <picofuse/sys.h>
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_mutex_t {
  mutex_t pmutex;
  bool init;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_mutex_t _sys_mutex_pool[SYS_MUTEX_CAPACITY] = {0};
static size_t _sys_mutex_pool_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static inline bool _sys_mutex_valid(const sys_mutex_t *mutex) {
  return mutex != NULL && mutex->init &&
         mutex_is_initialized((mutex_t *)&mutex->pmutex);
}

/** @brief Initializes the native Pico mutex stored in a pool slot. */
static inline bool _sys_mutex_init_handle(sys_mutex_t *mutex) {
  mutex_init(&mutex->pmutex);
  return mutex_is_initialized(&mutex->pmutex);
}

/**
 * @brief Deinitializes the native Pico mutex stored in a pool slot.
 *
 * Pico's mutex_t owns no external resources - mutex_init() assigns a fresh
 * striped spin lock and fully resets state on reuse - so there is nothing to
 * release here before the slot is returned to the pool.
 */
static inline void _sys_mutex_deinit_handle(sys_mutex_t *mutex) {
  (void)mutex;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Allocates and initializes a mutex from the static pool. */
sys_mutex_t *sys_mutex_init(void) {
  _sys_sync_pool_lock();

  for (size_t offset = 0; offset < SYS_MUTEX_CAPACITY; offset++) {
    size_t index = (_sys_mutex_pool_index + offset) % SYS_MUTEX_CAPACITY;
    sys_mutex_t *mutex = &_sys_mutex_pool[index];
    if (mutex->init) {
      continue;
    }

    mutex->init = true;
    if (!_sys_mutex_init_handle(mutex)) {
      mutex->init = false;
      _sys_sync_pool_unlock();
      return NULL;
    }

    _sys_mutex_pool_index = (index + 1) % SYS_MUTEX_CAPACITY;
    _sys_sync_pool_unlock();
    return mutex;
  }

  _sys_sync_pool_unlock();
  return NULL;
}

/** @brief Deinitializes a mutex and returns its pool slot. */
void sys_mutex_deinit(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return;
  }

  _sys_sync_pool_lock();
  if (!mutex->init) {
    _sys_sync_pool_unlock();
    return;
  }
  _sys_mutex_deinit_handle(mutex);
  mutex->init = false;
  _sys_sync_pool_unlock();
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Locks a mutex, blocking until it becomes available. */
bool sys_mutex_lock(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return false;
  }
  mutex_enter_blocking(&mutex->pmutex);
  return true;
}

/** @brief Attempts to lock a mutex without blocking. */
bool sys_mutex_trylock(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return false;
  }
  uint32_t owner_out;
  return mutex_try_enter(&mutex->pmutex, &owner_out);
}

/** @brief Unlocks a previously locked mutex. */
bool sys_mutex_unlock(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return false;
  }
  mutex_exit(&mutex->pmutex);
  return true;
}
