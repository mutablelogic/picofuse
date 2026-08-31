#include "private.h"
#include <picofuse/sys.h>
#include <stddef.h>

#ifdef SYSTEM_NAME_PICO
#include "../pico/sync.h"
#endif

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Locks the mutex protecting a queue's ring-buffer bookkeeping (see
 * private.h).
 *
 * On Pico, this enters the critical section already shared by the mutex/
 * cond/waitgroup pools (see sys/pico/sync.c) rather than locking
 * queue->mutex It makes this safe to call from IRQ context (HID timer/gpio
 * event sources): entering queue->mutex there could self-deadlock against a
 * consumer already holding it from thread context. */
bool _sys_event_queue_lock(sys_event_queue_t *queue) {
#ifdef SYSTEM_NAME_PICO
  (void)queue;
  _sys_sync_pool_lock();
  return true;
#else
  return sys_mutex_lock(queue->mutex);
#endif
}

/** @brief Unlocks the mutex protecting a queue's ring-buffer bookkeeping  */
bool _sys_event_queue_unlock(sys_event_queue_t *queue) {
#ifdef SYSTEM_NAME_PICO
  (void)queue;
  _sys_sync_pool_unlock();
  return true;
#else
  return sys_mutex_unlock(queue->mutex);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Locks a queue for manual inspection */
bool sys_event_queue_lock(sys_event_queue_t *queue) {
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return false;
  }

  return sys_mutex_lock(queue->mutex);
}

/** @brief Unlocks a queue after manual inspection */
bool sys_event_queue_unlock(sys_event_queue_t *queue) {
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return false;
  }

  return sys_mutex_unlock(queue->mutex);
}
