#include <hardware/platform_defs.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_atomic_t _sys_thread_core1_worker_started;
static sys_atomic_t _sys_thread_core1_worker_busy;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static void _sys_thread_wrapper(void);

/** @brief Ensure that the core 1 worker is running */
static inline void _sys_thread_ensure_core1_worker(void) {
  if (sys_atomic_get(&_sys_thread_core1_worker_started) != 0) {
    return;
  }
  multicore_reset_core1();
  multicore_launch_core1(_sys_thread_wrapper);
  sys_atomic_set(&_sys_thread_core1_worker_started, 1);
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/**
 * @brief Wrapper function to adapt sys_thread_func_t to Pico multicore function
 */
static void _sys_thread_wrapper(void) {
  for (;;) {
    sys_thread_func_t func =
        (sys_thread_func_t)(uintptr_t)multicore_fifo_pop_blocking();
    void *arg = (void *)(uintptr_t)multicore_fifo_pop_blocking();

    if (func == NULL) {
      sys_panicf("Pico thread wrapper: function pointer is NULL");
    }

    func(arg);
    sys_atomic_set(&_sys_thread_core1_worker_busy, 0);
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Get the number of available cores on the Pico
 */
uint8_t sys_thread_numcores(void) { return (uint8_t)NUM_CORES; }

/**
 * @brief Create a new thread. Always results in an error
 */
bool sys_thread_create(sys_thread_func_t func, void *arg) {
  (void)func;
  (void)arg;
  return false;
}

/**
 * @brief Create a new thread. Only supports core 1, returns false for any other
 * core or if func is NULL
 */
bool sys_thread_create_on_core(sys_thread_func_t func, void *arg,
                               uint8_t core) {
  if (func == NULL) {
    return false;
  }

  if (sys_thread_core() != 0) {
    return false;
  }

  if (core >= sys_thread_numcores()) {
    return false;
  }

  switch (core) {
  case 0:
    return false;
  case 1: {
    const uint64_t timeout_us = 1000000;

    if (sys_atomic_get(&_sys_thread_core1_worker_busy) != 0) {
      return false;
    }

    _sys_thread_ensure_core1_worker();
    sys_atomic_set(&_sys_thread_core1_worker_busy, 1);

    if (!multicore_fifo_push_timeout_us((uint32_t)(uintptr_t)func,
                                        timeout_us)) {
      sys_atomic_set(&_sys_thread_core1_worker_busy, 0);
      sys_panicf("Failed to push function pointer to core 1 FIFO");
      return false;
    }

    if (!multicore_fifo_push_timeout_us((uint32_t)(uintptr_t)arg, timeout_us)) {
      sys_atomic_set(&_sys_thread_core1_worker_busy, 0);
      sys_panicf("Failed to push argument to core 1 FIFO");
      return false;
    }

    return true;
  }
  default:
    return false;
  }
}

/**
 * @brief Get the current core number
 */
uint8_t sys_thread_core(void) { return (uint8_t)get_core_num(); }
