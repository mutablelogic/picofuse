#include "private.h"
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Returns whether a queue's mutex and condition variable were both
 * successfully constructed (see private.h). */
bool _sys_event_queue_valid_unlocked(sys_event_queue_t *queue) {
  return queue != NULL && queue->mutex != NULL && queue->not_empty != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Allocates and initializes a new event queue. */
sys_event_queue_t *sys_event_queue_init(size_t capacity) {
  if (capacity == 0 ||
      capacity > (SIZE_MAX - sizeof(sys_event_queue_t)) / sizeof(sys_event_t)) {
    return NULL;
  }

  size_t total_size =
      sizeof(sys_event_queue_t) + capacity * sizeof(sys_event_t);
  sys_event_queue_t *queue = (sys_event_queue_t *)sys_malloc(total_size);
  if (queue == NULL) {
    return NULL;
  }

  memset(queue, 0, total_size);
  queue->capacity = capacity;
  queue->mutex = sys_mutex_init();
  if (queue->mutex == NULL) {
    sys_free(queue);
    return NULL;
  }

  queue->not_empty = sys_cond_init();
  if (queue->not_empty == NULL) {
    sys_mutex_deinit(queue->mutex);
    sys_free(queue);
    return NULL;
  }

  return queue;
}

/** @brief Tears down an event queue and releases its resources. */
void sys_event_queue_deinit(sys_event_queue_t *queue) {
  if (queue == NULL) {
    return;
  }

  if (_sys_event_queue_valid_unlocked(queue)) {
    _sys_event_queue_lock(queue);
    queue->shutdown = true;
    _sys_event_queue_unlock(queue);
    sys_cond_broadcast(queue->not_empty);
  }

  if (queue->not_empty != NULL) {
    sys_cond_deinit(queue->not_empty);
  }
  if (queue->mutex != NULL) {
    sys_mutex_deinit(queue->mutex);
  }
  sys_free(queue);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Prevent future pushes and wake blocked consumers */
void sys_event_queue_shutdown(sys_event_queue_t *queue) {
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return;
  }

  _sys_event_queue_lock(queue);
  queue->shutdown = true;
  _sys_event_queue_unlock(queue);

  sys_cond_broadcast(queue->not_empty);
}
