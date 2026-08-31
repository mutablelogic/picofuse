#pragma once
#include <picofuse/sys/cond.h>
#include <picofuse/sys/event.h>
#include <picofuse/sys/mutex.h>
#include <stdbool.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_event_queue_t {
  size_t capacity;
  size_t head;
  size_t tail;
  size_t count;
  sys_mutex_t *mutex;
  sys_cond_t *not_empty;
  bool shutdown;
  sys_event_t items[];
};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Returns whether `queue`'s mutex and condition variable were both
 * successfully constructed - false for a queue that failed partway through
 * sys_event_queue_init(). Caller must not hold `queue->mutex`. */
bool _sys_event_queue_valid_unlocked(sys_event_queue_t *queue);

/** @brief Locks the mutex protecting a queue's ring-buffer bookkeeping
 * (head/tail/count/shutdown).  */
bool _sys_event_queue_lock(sys_event_queue_t *queue);

/** @brief Unlocks the mutex protecting a queue's ring-buffer bookkeeping. */
bool _sys_event_queue_unlock(sys_event_queue_t *queue);
