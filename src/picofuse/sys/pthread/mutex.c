#include "mutex.h"
#include <picofuse/sys.h>
#include <pthread.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static pthread_mutex_t _sys_mutex_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static sys_mutex_t _sys_mutex_pool[SYS_MUTEX_CAPACITY] = {0};
static size_t _sys_mutex_pool_next_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Initializes the native pthread mutex stored in a pool slot. */
static inline bool _sys_mutex_init_handle(sys_mutex_t *mutex) {
  pthread_mutexattr_t attr;
  if (pthread_mutexattr_init(&attr) != 0) {
    return false;
  }

  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
    pthread_mutexattr_destroy(&attr);
    return false;
  }

  int result = pthread_mutex_init(&mutex->pmutex, &attr);
  pthread_mutexattr_destroy(&attr);
  return result == 0;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Allocates and initializes a mutex from the static pool. */
sys_mutex_t *sys_mutex_init(void) {
  if (pthread_mutex_lock(&_sys_mutex_pool_lock) != 0) {
    return NULL;
  }

  for (size_t offset = 0; offset < SYS_MUTEX_CAPACITY; offset++) {
    size_t index = (_sys_mutex_pool_next_index + offset) % SYS_MUTEX_CAPACITY;
    sys_mutex_t *mutex = &_sys_mutex_pool[index];
    if (mutex->init) {
      continue;
    }

    mutex->init = true;
    if (!_sys_mutex_init_handle(mutex)) {
      mutex->init = false;
      pthread_mutex_unlock(&_sys_mutex_pool_lock);
      return NULL;
    }

    _sys_mutex_pool_next_index = (index + 1) % SYS_MUTEX_CAPACITY;
    pthread_mutex_unlock(&_sys_mutex_pool_lock);
    return mutex;
  }

  pthread_mutex_unlock(&_sys_mutex_pool_lock);
  return NULL;
}

/** @brief Deinitializes a mutex and returns its pool slot. */
void sys_mutex_deinit(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return;
  }

  int lock_result = pthread_mutex_lock(&_sys_mutex_pool_lock);
  if (lock_result != 0) {
    return;
  }

  if (!mutex->init) {
    pthread_mutex_unlock(&_sys_mutex_pool_lock);
    return;
  }

  int destroy_result = pthread_mutex_destroy(&mutex->pmutex);
  if (destroy_result != 0) {
    pthread_mutex_unlock(&_sys_mutex_pool_lock);
    return;
  }

  mutex->init = false;
  pthread_mutex_unlock(&_sys_mutex_pool_lock);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Locks a mutex, blocking until it becomes available. */
bool sys_mutex_lock(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return false;
  }
  return pthread_mutex_lock(&mutex->pmutex) == 0;
}

/** @brief Attempts to lock a mutex without blocking. */
bool sys_mutex_trylock(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return false;
  }
  return pthread_mutex_trylock(&mutex->pmutex) == 0;
}

/** @brief Unlocks a previously locked mutex. */
bool sys_mutex_unlock(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return false;
  }
  return pthread_mutex_unlock(&mutex->pmutex) == 0;
}
