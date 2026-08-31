#include "private.h"
#include <picofuse/sys.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Return the current event count */
size_t sys_event_queue_size(sys_event_queue_t *queue) {
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return 0;
  }

  _sys_event_queue_lock(queue);
  size_t size = queue->count;
  _sys_event_queue_unlock(queue);
  return size;
}

/** @brief Report whether a queue is empty */
bool sys_event_queue_empty(sys_event_queue_t *queue) {
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return true;
  }

  _sys_event_queue_lock(queue);
  bool empty = queue->count == 0;
  _sys_event_queue_unlock(queue);
  return empty;
}
