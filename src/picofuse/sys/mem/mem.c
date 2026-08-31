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

/**
 * @brief Return the tail arena in a chain.
 * @param head Head of the chain.
 * @param stats Optional stats populated for the returned tail arena.
 * @return Tail arena, or `NULL` when the chain is empty.
 */
static sys_mem_arena_t *_sys_mem_default_tail(sys_mem_arena_t *head,
                                              sys_mem_arena_stats_t *stats) {
  if (head == NULL) {
    return NULL;
  }

  sys_mem_arena_t *current = head;
  sys_mem_arena_stats_t current_stats = {0};
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
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocated block, or `NULL` on failure.
 */
static void *_sys_mem_default_alloc(size_t size) {
  if (size == 0) {
    return NULL;
  }

  while (true) {
    sys_mem_arena_t *head = _sys_mem_default_head;
    sys_mem_arena_stats_t tail_stats = {0};
    sys_mem_arena_t *tail = _sys_mem_default_tail(head, &tail_stats);
    if (tail == NULL) {
      return NULL;
    }

    // Prefer the most recently grown arena first - it's the one most
    // likely to have room, since earlier ones only get used again once
    // later ones are also full.
    sys_mem_arena_t *current = tail;
    while (current != NULL) {
      void *ptr = sys_mem_arena_alloc(current, size);
      if (ptr != NULL) {
        return ptr;
      }
      current = _sys_mem_arena_prev(current, NULL);
    }

    size_t arena_size = 0;
    if (!_sys_mem_default_growth_size(tail_stats.size_bytes, size,
                                      &arena_size)) {
      return NULL;
    }

    sys_mem_arena_t *next = sys_mem_arena_init(arena_size, tail, NULL, NULL);
    if (next != NULL) {
      sys_debugf("mem", "default arena grew: +%zu bytes (needed %zu)",
                 arena_size, size);
      return sys_mem_arena_alloc(next, size);
    }

    // Growth failed (e.g. the system allocator is itself exhausted) - if
    // nothing else grew the chain concurrently in the meantime, there's
    // nothing further to try.
    if (_sys_mem_default_tail(_sys_mem_default_head, NULL) == tail) {
      return NULL;
    }
  }
}

/**
 * @brief Find the arena that owns a payload pointer.
 * @param ptr Payload pointer to locate.
 * @param size Optional allocation size populated for the owning arena.
 * @return Arena that owns `ptr`, or `NULL` when not found (including when
 *         no default arena is configured).
 */
static sys_mem_arena_t *_sys_mem_default_owner(void *ptr, size_t *size) {
  sys_mem_arena_t *tail = _sys_mem_default_tail(_sys_mem_default_head, NULL);
  while (tail != NULL) {
    size_t alloc_size = _sys_mem_arena_alloc_size(tail, ptr);
    if (alloc_size != 0) {
      if (size != NULL) {
        *size = alloc_size;
      }
      return tail;
    }
    tail = _sys_mem_arena_prev(tail, NULL);
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
  sys_mem_arena_t *tail = _sys_mem_default_head;
  _sys_mem_default_head = NULL;
  if (tail == NULL) {
    return;
  }

  // sys_mem_arena_delete() only ever accepts the current tail of a chain,
  // so find it first, then delete backward (LIFO) rather than forward.
  tail = _sys_mem_default_tail(tail, NULL);

  while (tail != NULL) {
    sys_mem_arena_stats_t stats = {0};
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
  if (_sys_mem_default_head == NULL) {
    return malloc(size);
  }
  return _sys_mem_default_alloc(size);
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

  if (_sys_mem_default_head != NULL) {
    size_t current_size = 0;
    sys_mem_arena_t *owner = _sys_mem_default_owner(ptr, &current_size);
    // Once a default arena is configured, every pointer passed here must
    // belong to it - mixing in one allocated before configuration (or by
    // some other allocator entirely) is a caller bug, not a case to paper
    // over silently.
    sys_assert(owner != NULL);

    void *resized = sys_mem_arena_realloc(owner, ptr, size);
    if (resized != NULL) {
      return resized;
    }

    void *replacement = _sys_mem_default_alloc(size);
    if (replacement == NULL) {
      return NULL;
    }

    size_t copy_size = current_size < size ? current_size : size;
    memcpy(replacement, ptr, copy_size);
    sys_mem_arena_free(owner, ptr);
    return replacement;
  }

  return realloc(ptr, size);
}

/** @brief Release an allocation owned by the default allocator. */
void sys_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  if (_sys_mem_default_head != NULL) {
    sys_mem_arena_t *owner = _sys_mem_default_owner(ptr, NULL);
    // See sys_realloc()'s comment above - this must never be NULL once a
    // default arena is configured.
    sys_assert(owner != NULL);
    sys_mem_arena_free(owner, ptr);
    return;
  }

  free(ptr);
}
