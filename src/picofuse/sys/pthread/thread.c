#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <picofuse/sys.h>

#include <pthread.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#ifdef __linux__
#include <sched.h>
#include <sys/sysinfo.h>
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct {
  sys_thread_func_t func;
  void *arg;
  bool in_use;
} _sys_thread_slot_t;

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static pthread_mutex_t _sys_thread_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static _sys_thread_slot_t _sys_thread_pool[SYS_THREAD_CAPACITY];
static size_t _sys_thread_pool_next_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Clamps a raw core-count query result to a valid uint8_t. */
static uint8_t _sys_thread_clamp_core_count(long count) {
  if (count <= 0) {
    return 1;
  }

  return (uint8_t)(count > UINT8_MAX ? UINT8_MAX : count);
}

/** @brief Returns a claimed pool slot back to the static pool. */
static void _sys_thread_release_slot(_sys_thread_slot_t *slot) {
  sys_assert(slot != NULL);
  sys_assert(pthread_mutex_lock(&_sys_thread_pool_lock) == 0);
  slot->func = NULL;
  slot->arg = NULL;
  slot->in_use = false;
  pthread_mutex_unlock(&_sys_thread_pool_lock);
}

/** @brief Claims a free pool slot and stores the thread's func/arg in it. */
static _sys_thread_slot_t *_sys_thread_claim_slot(sys_thread_func_t func,
                                                   void *arg) {
  sys_assert(func != NULL);
  if (pthread_mutex_lock(&_sys_thread_pool_lock) != 0) {
    return NULL;
  }

  for (size_t offset = 0; offset < SYS_THREAD_CAPACITY; offset++) {
    size_t index =
        (_sys_thread_pool_next_index + offset) % SYS_THREAD_CAPACITY;
    _sys_thread_slot_t *slot = &_sys_thread_pool[index];
    if (slot->in_use) {
      continue;
    }

    slot->func = func;
    slot->arg = arg;
    slot->in_use = true;
    _sys_thread_pool_next_index = (index + 1) % SYS_THREAD_CAPACITY;
    pthread_mutex_unlock(&_sys_thread_pool_lock);
    return slot;
  }

  pthread_mutex_unlock(&_sys_thread_pool_lock);
  return NULL;
}

/** @brief Wrapper function to adapt sys_thread_func_t to pthread's entry
 * point signature. */
static void *_sys_thread_wrapper(void *arg) {
  _sys_thread_slot_t *slot = (_sys_thread_slot_t *)arg;
  sys_thread_func_t func = slot->func;
  void *thread_arg = slot->arg;

  func(thread_arg);
  _sys_thread_release_slot(slot);
  return NULL;
}

/** @brief Claims a pool slot and creates a detached pthread for it. */
static bool _sys_thread_create_with_attr(sys_thread_func_t func, void *arg,
                                          pthread_attr_t *attr,
                                          pthread_t *thread_out) {
  _sys_thread_slot_t *slot = _sys_thread_claim_slot(func, arg);
  if (slot == NULL) {
    return false;
  }

  int result = pthread_create(thread_out, attr, _sys_thread_wrapper, slot);
  if (result != 0) {
    _sys_thread_release_slot(slot);
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Returns the number of CPU cores available on the host system. */
uint8_t sys_thread_numcores(void) {
#ifdef __APPLE__
  int mac_cores = 0;
  size_t len = sizeof(mac_cores);
  if (sysctlbyname("hw.ncpu", &mac_cores, &len, NULL, 0) == 0) {
    return _sys_thread_clamp_core_count(mac_cores);
  }
#elif defined(__linux__)
  int linux_cores = get_nprocs();
  if (linux_cores > 0) {
    return _sys_thread_clamp_core_count(linux_cores);
  }
#endif

  return _sys_thread_clamp_core_count(sysconf(_SC_NPROCESSORS_ONLN));
}

/** @brief Creates a detached, fire-and-forget thread on any available core. */
bool sys_thread_create(sys_thread_func_t func, void *arg) {
  if (func == NULL) {
    return false;
  }

  pthread_t thread;
  pthread_attr_t attr;

  if (pthread_attr_init(&attr) != 0) {
    return false;
  }

  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
    pthread_attr_destroy(&attr);
    return false;
  }

  bool ok = _sys_thread_create_with_attr(func, arg, &attr, &thread);
  pthread_attr_destroy(&attr);
  return ok;
}

/** @brief Creates a detached, fire-and-forget thread pinned to a core. */
bool sys_thread_create_on_core(sys_thread_func_t func, void *arg,
                                uint8_t core) {
  if (func == NULL || core >= sys_thread_numcores()) {
    return false;
  }

  pthread_attr_t attr;

  if (pthread_attr_init(&attr) != 0) {
    return false;
  }

  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
    pthread_attr_destroy(&attr);
    return false;
  }

#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  if (pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset) != 0) {
    pthread_attr_destroy(&attr);
    return false;
  }
#else
  pthread_attr_destroy(&attr);
  (void)core;
  return false;
#endif

  pthread_t thread;
  bool ok = _sys_thread_create_with_attr(func, arg, &attr, &thread);
  pthread_attr_destroy(&attr);
  return ok;
}

/** @brief Gets the CPU core number the current thread is running on. */
uint8_t sys_thread_core(void) {
#ifdef __linux__
  int cpu = sched_getcpu();
  if (cpu >= 0) {
    return (uint8_t)(cpu > UINT8_MAX ? UINT8_MAX : cpu);
  }
#endif

  return 0;
}
