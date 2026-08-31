#include "private.h"
#include <picofuse/sys.h>
#include <stddef.h>
#include <stdint.h>

// How often sys_event_queue_pop()/sys_event_queue_timed_pop() re-check the
// queue while waiting, since a wakeup can rarely be missed
#define _SYS_EVENT_QUEUE_POP_POLL_MS 50u

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Returns the ring-buffer index following `index`, wrapping at
 * queue->capacity. */
static size_t _sys_event_queue_next_index(sys_event_queue_t *queue,
                                          size_t index) {
  size_t next = index + 1;
  return next == queue->capacity ? 0 : next;
}

/** @brief Pops the event at the tail and advances it, or returns NULL if the
 * queue is empty. Caller must hold the queue's lock. */
static sys_event_t _sys_event_queue_pop_locked(sys_event_queue_t *queue) {
  if (queue->count == 0) {
    return NULL;
  }

  sys_event_t event = queue->items[queue->tail];
  queue->items[queue->tail] = NULL;
  queue->tail = _sys_event_queue_next_index(queue, queue->tail);
  queue->count--;
  return event;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Push an event, overwriting the oldest entry if full (see
 * sys/event.h). */
bool sys_event_queue_push(sys_event_queue_t *queue, sys_event_t event) {
  if (event == NULL || !_sys_event_queue_valid_unlocked(queue)) {
    return false;
  }

  _sys_event_queue_lock(queue);

  if (queue->shutdown) {
    _sys_event_queue_unlock(queue);
    return false;
  }

  queue->items[queue->head] = event;
  queue->head = _sys_event_queue_next_index(queue, queue->head);
  if (queue->count == queue->capacity) {
    queue->tail = queue->head;
  } else {
    queue->count++;
  }

  _sys_event_queue_unlock(queue);

  return sys_cond_broadcast(queue->not_empty);
}

/** @brief Try to push an event without overwriting (see sys/event.h). */
bool sys_event_queue_try_push(sys_event_queue_t *queue, sys_event_t event) {
  // Safe to call from IRQ context (HID timer/gpio event sources): the
  // queue lock never blocks (see _sys_event_queue_lock), and
  // sys_cond_broadcast() is IRQ-safe too (see src/sys/pico/cond.c).
  if (event == NULL || !_sys_event_queue_valid_unlocked(queue)) {
    return false;
  }

  _sys_event_queue_lock(queue);

  if (queue->shutdown || queue->count == queue->capacity) {
    _sys_event_queue_unlock(queue);
    return false;
  }

  queue->items[queue->head] = event;
  queue->head = _sys_event_queue_next_index(queue, queue->head);
  queue->count++;

  _sys_event_queue_unlock(queue);

  return sys_cond_broadcast(queue->not_empty);
}

/** @brief Peek at the next queued event without removing it. */
sys_event_t sys_event_queue_peek(sys_event_queue_t *queue) {
  // Deliberately unlocked: pairs with sys_event_queue_lock()/unlock() (see
  // their doc comments), which callers already hold around this call.
  // Locking here too would self-deadlock against that outer lock.
  if (!_sys_event_queue_valid_unlocked(queue) || queue->count == 0) {
    return NULL;
  }

  return queue->items[queue->tail];
}

/** @brief Pop the next event, blocking until one is available or shutdown. */
sys_event_t sys_event_queue_pop(sys_event_queue_t *queue) {
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return NULL;
  }

  // A bounded poll loop rather than one indefinite wait: pushes from IRQ
  // context no longer serialize with this function's "check queue, then
  // wait" step via queue->mutex (see sys_event_queue_try_push), so a wakeup
  // can rarely be missed. Re-checking the queue every
  // _SYS_EVENT_QUEUE_POP_POLL_MS bounds the resulting extra latency instead
  // of risking an indefinite hang.
  for (;;) {
    _sys_event_queue_lock(queue);
    if (queue->count > 0) {
      sys_event_t event = _sys_event_queue_pop_locked(queue);
      _sys_event_queue_unlock(queue);
      return event;
    }
    bool shutdown = queue->shutdown;
    _sys_event_queue_unlock(queue);

    if (shutdown) {
      return NULL;
    }

    if (!sys_mutex_lock(queue->mutex)) {
      return NULL;
    }
    sys_cond_timedwait(queue->not_empty, queue->mutex,
                       _SYS_EVENT_QUEUE_POP_POLL_MS);
    sys_mutex_unlock(queue->mutex);
  }
}

/** @brief Pop the next event without blocking. */
sys_event_t sys_event_queue_try_pop(sys_event_queue_t *queue) {
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return NULL;
  }

  _sys_event_queue_lock(queue);
  sys_event_t event = _sys_event_queue_pop_locked(queue);
  _sys_event_queue_unlock(queue);
  return event;
}

/** @brief Pop the next event with a timeout. */
sys_event_t sys_event_queue_timed_pop(sys_event_queue_t *queue,
                                      uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    return sys_event_queue_pop(queue);
  }
  if (!_sys_event_queue_valid_unlocked(queue)) {
    return NULL;
  }

  uint64_t deadline = sys_timestamp_ms() + timeout_ms;

  // See sys_event_queue_pop(): queue->mutex/not_empty are a best-effort,
  // low-latency wakeup hint here, not required for correctness. This loop
  // always re-checks the actual (lock-protected) queue below regardless of
  // whether the wait below is signaled or simply times out, so a wakeup
  // that races a concurrent IRQ-context push just costs one extra bounded
  // iteration, never a lost event.
  for (;;) {
    _sys_event_queue_lock(queue);
    if (queue->count > 0) {
      sys_event_t event = _sys_event_queue_pop_locked(queue);
      _sys_event_queue_unlock(queue);
      return event;
    }
    bool shutdown = queue->shutdown;
    _sys_event_queue_unlock(queue);

    if (shutdown) {
      return NULL;
    }

    uint64_t now = sys_timestamp_ms();
    if (now >= deadline) {
      return NULL;
    }

    uint64_t remaining = deadline - now;
    if (remaining > UINT32_MAX) {
      remaining = UINT32_MAX;
    }

    if (!sys_mutex_lock(queue->mutex)) {
      return NULL;
    }
    sys_cond_timedwait(queue->not_empty, queue->mutex, (uint32_t)remaining);
    sys_mutex_unlock(queue->mutex);
  }
}
