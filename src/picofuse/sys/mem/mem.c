#include "private.h"
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef SYS_MEM_ARENA_GROWTH_MIN_SLACK
#define SYS_MEM_ARENA_GROWTH_MIN_SLACK ((size_t)1024u)
#endif

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// NULL until _sys_mem_module_init() succeeds, at which point sys_malloc() and
// friends allocate from this chain instead of the system allocator.
static sys_mem_arena_t *_sys_mem_default_head = NULL;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Return the tail arena in a chain. Each step is self-locking, which
 * is safe here: nothing but growth (handled separately, with its own retry)
 * ever changes which arena is the tail. */
static sys_mem_arena_t *_sys_mem_default_tail(sys_mem_arena_t *head,
                                              sys_mem_stats_t *stats) {
  sys_mem_arena_t *current = head;
  sys_mem_stats_t current_stats = {0};
  while (true) {
    sys_mem_arena_t *next = sys_mem_arena_next(current, &current_stats);
    if (next == NULL) {
      if (stats != NULL) {
        *stats = current_stats;
      }
      return current;
    }
    current = next;
  }
}

/**
 * @brief Compute the size of a newly appended arena.
 * @param previous_size Payload size of the current tail arena.
 * @param required_size Allocation size that triggered growth.
 * @param arena_size Output size for the new arena payload.
 * @return `true` when a size was produced, otherwise `false`.
 */
static bool _sys_mem_default_growth_size(size_t previous_size,
                                         size_t required_size,
                                         size_t *arena_size) {
  if (arena_size == NULL || required_size == 0) {
    return false;
  }

  size_t candidate = previous_size;
  if (required_size > candidate) {
    size_t slack = required_size / 2u;
    if (slack < SYS_MEM_ARENA_GROWTH_MIN_SLACK) {
      slack = SYS_MEM_ARENA_GROWTH_MIN_SLACK;
    }

    if (required_size > SIZE_MAX - slack) {
      candidate = required_size;
    } else {
      candidate = required_size + slack;
    }
  }

  if (candidate == 0) {
    return false;
  }

  *arena_size = candidate;
  return true;
}

/**
 * @brief Allocate from the default arena chain, growing it when necessary.
 * @param head Head of the default chain (never NULL).
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocated block, or `NULL` on failure.
 *
 * Each per-arena step below (sys_mem_arena_next/_sys_mem_arena_prev/
 * sys_mem_arena_alloc) locks and unlocks independently, which is safe since
 * a pointer is never touched by anyone but the thread that holds it. The one
 * place two threads can genuinely interfere is appending a new arena to the
 * chain: if that races and loses (sys_mem_arena_init() returns NULL because
 * the tail moved out from under it), the loop below simply retries, which
 * picks up whichever arena the winner appended.
 */
static void *_sys_mem_default_alloc(sys_mem_arena_t *head, size_t size) {
  if (size == 0) {
    return NULL;
  }

  while (true) {
    sys_mem_stats_t tail_stats = {0};
    sys_mem_arena_t *tail = _sys_mem_default_tail(head, &tail_stats);

    // Prefer the most recently grown arena first - it's the one most likely
    // to have room, since earlier ones only get used again once later ones
    // are also full.
    for (sys_mem_arena_t *current = tail; current != NULL;
         current = _sys_mem_arena_prev(current, NULL)) {
      void *ptr = sys_mem_arena_alloc(current, size);
      if (ptr != NULL) {
        return ptr;
      }
    }

    size_t arena_size = 0;
    if (!_sys_mem_default_growth_size(tail_stats.size_bytes, size,
                                      &arena_size)) {
      return NULL;
    }

    sys_mem_arena_t *next = sys_mem_arena_init(arena_size, tail, NULL, NULL);
    if (next == NULL) {
      // Either another thread already appended past `tail` (retry picks it
      // up above) or the underlying allocator is genuinely out of memory
      // (retry will observe the same tail and fail identically).
      if (sys_mem_arena_next(tail, NULL) == NULL) {
        return NULL;
      }
      continue;
    }

    sys_debugf("mem", "default arena grew: +%zu bytes (needed %zu)",
               arena_size, size);
    void *ptr = sys_mem_arena_alloc(next, size);
    if (ptr != NULL) {
      return ptr;
    }
  }
}

/**
 * @brief Find the arena that owns a payload pointer.
 * @param head Head of the default chain (never NULL).
 * @param ptr Payload pointer to locate.
 * @param size Optional allocation size populated for the owning arena.
 * @return Arena that owns `ptr`, or `NULL` when not found.
 */
static sys_mem_arena_t *_sys_mem_default_owner(sys_mem_arena_t *head,
                                               void *ptr, size_t *size) {
  for (sys_mem_arena_t *current = _sys_mem_default_tail(head, NULL);
       current != NULL; current = _sys_mem_arena_prev(current, NULL)) {
    size_t alloc_size = _sys_mem_arena_alloc_size(current, ptr);
    if (alloc_size != 0) {
      if (size != NULL) {
        *size = alloc_size;
      }
      return current;
    }
  }

  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/**
 * @brief Initialize the process-wide default arena.
 * @param capacity Arena capacity in bytes, or `0` to leave the default
 *                 arena unconfigured (sys_malloc() and friends then route
 *                 straight through to the system allocator).
 * @param malloc_fn Backing allocator used for the first arena.
 * @param free_fn Backing deallocator paired with `malloc_fn`.
 * @return `true` when the default arena is ready (or deliberately left
 *         unconfigured because `capacity` is `0`), `false` on failure.
 */
bool _sys_mem_module_init(size_t capacity, void *(*malloc_fn)(size_t),
                          void (*free_fn)(void *)) {
  if (_sys_mem_default_head != NULL) {
    return true;
  }

  if (capacity == 0) {
    return true;
  }
  if (malloc_fn == NULL || free_fn == NULL) {
    return false;
  }

  sys_mem_arena_t *arena =
      sys_mem_arena_init(capacity, NULL, malloc_fn, free_fn);
  if (arena == NULL) {
    return false;
  }

  _sys_mem_default_head = arena;
  sys_debugf("mem", "default arena configured: %zu bytes", capacity);
  return true;
}

/**
 * @brief Tear down the process-wide default arena chain, if configured.
 */
void _sys_mem_module_exit(void) {
  sys_mem_arena_t *head = _sys_mem_default_head;
  _sys_mem_default_head = NULL;
  if (head == NULL) {
    return;
  }

  // sys_mem_arena_delete() only ever accepts the current tail of a chain,
  // so find it first, then delete backward (LIFO) rather than forward.
  sys_mem_arena_t *tail = _sys_mem_default_tail(head, NULL);

  while (tail != NULL) {
    sys_mem_stats_t stats = {0};
    sys_mem_arena_t *prev = _sys_mem_arena_prev(tail, &stats);
    sys_debugf("mem", "default arena %p torn down: size=%zu used=%zu "
                      "allocations=%zu",
               (void *)tail, stats.size_bytes, stats.used_bytes,
               stats.allocations);
    sys_mem_arena_delete(tail);
    tail = prev;
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Allocate memory from the default allocator. */
void *sys_malloc(size_t size) {
  sys_mem_arena_t *head = _sys_mem_default_head;
  if (head == NULL) {
    return malloc(size);
  }
  return _sys_mem_default_alloc(head, size);
}

/** @brief Allocate zero-initialized memory from the default allocator. */
void *sys_calloc(size_t count, size_t size) {
  if (count != 0 && size > SIZE_MAX / count) {
    return NULL;
  }
  size_t total_size = count * size;

  if (_sys_mem_default_head == NULL) {
    return calloc(count, size);
  }

  void *ptr = sys_malloc(total_size);
  if (ptr != NULL) {
    memset(ptr, 0, total_size);
  }
  return ptr;
}

/** @brief Resize an allocation owned by the default allocator. */
void *sys_realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return sys_malloc(size);
  }
  if (size == 0) {
    sys_free(ptr);
    return NULL;
  }

  sys_mem_arena_t *head = _sys_mem_default_head;
  if (head == NULL) {
    return realloc(ptr, size);
  }

  size_t current_size = 0;
  sys_mem_arena_t *owner = _sys_mem_default_owner(head, ptr, &current_size);
  // Once a default arena is configured, every pointer passed here must
  // belong to it - mixing in one allocated before configuration (or by
  // some other allocator entirely) is a caller bug, not a case to paper
  // over silently.
  sys_assert(owner != NULL);

  void *resized = sys_mem_arena_realloc(owner, ptr, size);
  if (resized != NULL) {
    return resized;
  }

  void *replacement = _sys_mem_default_alloc(head, size);
  if (replacement == NULL) {
    return NULL;
  }

  size_t copy_size = current_size < size ? current_size : size;
  memcpy(replacement, ptr, copy_size);
  sys_mem_arena_free(owner, ptr);
  return replacement;
}

/** @brief Release an allocation owned by the default allocator. */
void sys_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  sys_mem_arena_t *head = _sys_mem_default_head;
  if (head == NULL) {
    free(ptr);
    return;
  }

  sys_mem_arena_t *owner = _sys_mem_default_owner(head, ptr, NULL);
  // See sys_realloc()'s comment above - this must never be NULL once a
  // default arena is configured.
  sys_assert(owner != NULL);
  sys_mem_arena_free(owner, ptr);
}
