#include <pico/critical_section.h>
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

static critical_section_t _sys_mutex_pool_lock;
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

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Initializes the Pico mutex pool lock. */
void _sys_mutex_module_init(void) {
  critical_section_init(&_sys_mutex_pool_lock);
}

/** @brief Deinitializes the Pico mutex pool lock. */
void _sys_mutex_module_deinit(void) {
  critical_section_deinit(&_sys_mutex_pool_lock);
}

/** @brief Allocates and initializes a mutex from the static pool. */
sys_mutex_t *sys_mutex_init(void) {
  critical_section_enter_blocking(&_sys_mutex_pool_lock);

  for (size_t offset = 0; offset < SYS_MUTEX_CAPACITY; offset++) {
    size_t index = (_sys_mutex_pool_index + offset) % SYS_MUTEX_CAPACITY;
    sys_mutex_t *mutex = &_sys_mutex_pool[index];
    if (mutex->init) {
      continue;
    }

    mutex->init = true;
    if (!_sys_mutex_init_handle(mutex)) {
      mutex->init = false;
      critical_section_exit(&_sys_mutex_pool_lock);
      return NULL;
    }

    _sys_mutex_pool_index = (index + 1) % SYS_MUTEX_CAPACITY;
    critical_section_exit(&_sys_mutex_pool_lock);
    return mutex;
  }

  critical_section_exit(&_sys_mutex_pool_lock);
  return NULL;
}

/** @brief Deinitializes a mutex and returns its pool slot. */
void sys_mutex_deinit(sys_mutex_t *mutex) {
  if (!_sys_mutex_valid(mutex)) {
    return;
  }

  critical_section_enter_blocking(&_sys_mutex_pool_lock);
  mutex->init = false;
  critical_section_exit(&_sys_mutex_pool_lock);
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
