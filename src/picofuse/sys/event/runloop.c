#include <picofuse/sys.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// How often worker 0 re-checks the queue while idle, so poll_fn (if any)
// keeps firing at a steady cadence. Matches the granularity
// sys_event_queue_timed_pop() already re-checks at internally, so this adds
// no extra latency of its own.
#define _SYS_RUNLOOP_POLL_INTERVAL_MS 50u

///////////////////////////////////////////////////////////////////////////////
// TYPES

/** @brief Per-worker launch context, indexed by worker_number. Passed
 * explicitly to each worker's thread entry point rather than having it
 * reach for the singleton by name, so the worker's dependencies are visible
 * in its own signature. `runloop` is read-only from a worker's point of
 * view - only sys_runloop_run()/_sys_runloop_init()/_sys_runloop_deinit(),
 * on the calling thread, ever mutate the singleton itself. */
typedef struct sys_runloop_worker_t {
  const struct sys_runloop_t *runloop;
  uint32_t worker_number;
} sys_runloop_worker_t;

/**
 * @brief Process-wide run loop state.
 *
 * `queue` is borrowed from the caller of sys_runloop_run() (never owned) and
 * doubles as the "is the run loop running" flag: NULL before the first call
 * to sys_runloop_run() and after it returns, non-NULL for the duration of
 * the call. Unlike similar single-writer-at-boot singletons elsewhere in
 * this codebase (e.g. mem.c's default arena, set once before any other
 * thread exists), sys_runloop_post()/sys_runloop_shutdown() can legitimately
 * be called by threads the caller started *before* sys_runloop_run(), so
 * reading/writing `queue` needs real synchronization - see `lock` below.
 *
 * `shutdown_requested` only matters to worker 0: sys_event_queue_pop()
 * (used by every other worker) is unambiguous - it returns NULL only once
 * shut down and drained - but worker 0 uses sys_event_queue_timed_pop() so
 * poll_fn keeps firing while idle, and *that* returns NULL both on a
 * plain timeout and once shut down and drained. This flag, plus rechecking
 * sys_event_queue_empty(), is what tells worker 0's loop the two apart. It's
 * a sys_atomic_t (unlike `queue`) because it's self-contained - nothing
 * else needs to be ordered relative to it - so the relaxed atomicity
 * sys_atomic_t provides is already enough.
 *
 * `workers` tracks only the additional workers (index 1..num_workers-1);
 * the calling thread runs worker 0 itself, synchronously, inside
 * sys_runloop_run(). It's never touched by any thread this module didn't
 * itself create (only by the run()-calling thread and by workers reading
 * it via their own worker_ctx, both already covered by the happens-before
 * edge thread creation establishes), so it doesn't need `lock`'s
 * protection.
 *
 * `worker_ctx` holds each additional worker's launch context, in static
 * storage rather than a stack or heap allocation per launch: it needs to
 * outlive the loop iteration that starts the thread (the new thread may not
 * read its arg until well after sys_thread_create*() returns), and unlike a
 * heap allocation there's nothing to free afterward.
 */
typedef struct sys_runloop_t {
  sys_event_queue_t *queue;
  sys_runloop_init_func_t init_fn;
  sys_runloop_event_func_t event_fn;
  sys_runloop_poll_func_t poll_fn;
  sys_runloop_exit_func_t exit_fn;
  sys_waitgroup_t *workers;
  sys_atomic_t exit_value;
  sys_atomic_t shutdown_requested;
  sys_runloop_worker_t worker_ctx[SYS_THREAD_CAPACITY];
} sys_runloop_t;

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_runloop_t _sys_runloop = {0};

// Guards `_sys_runloop.queue` (and the other fields _sys_runloop_init()/
// _sys_runloop_deinit() populate alongside it) against concurrent access
// from threads sys_runloop_run() didn't itself create. Created once by
// _sys_runloop_module_init(), called from sys_init() before any
// application thread can exist.
static sys_mutex_t *_sys_runloop_lock = NULL;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Entry point for workers 1..num_workers-1. Drains the queue with a
 * plain blocking pop() - unlike worker 0, it never needs to poll, so NULL
 * unambiguously means "shut down and drained". */
static void _sys_runloop_worker(void *arg) {
  const sys_runloop_worker_t *worker = arg;
  const sys_runloop_t *runloop = worker->runloop;
  uint8_t worker_index = (uint8_t)worker->worker_number;

  sys_debugf("runloop", "worker %u starting", worker_index);
  if (runloop->init_fn != NULL) {
    runloop->init_fn(worker_index);
  }

  while (true) {
    sys_event_t event = sys_event_queue_pop(runloop->queue);
    if (event == NULL) {
      break;
    }
    runloop->event_fn(event);
  }

  if (runloop->exit_fn != NULL) {
    runloop->exit_fn(worker_index);
  }

  sys_debugf("runloop", "worker %u exiting", worker_index);
  sys_waitgroup_done(runloop->workers);
}

/** @brief Starts a single worker, pinned to a specific core on Pico (where
 * core assignment is meaningful and limited to 2 cores) or as an ordinary
 * unpinned thread everywhere else (where "core i" isn't a useful concept for
 * an arbitrary worker index). */
static bool _sys_runloop_start_worker(uint8_t worker_index, void *ctx) {
#if defined(SYSTEM_NAME_PICO)
  return sys_thread_create_on_core(_sys_runloop_worker, ctx, worker_index);
#else
  (void)worker_index;
  return sys_thread_create(_sys_runloop_worker, ctx);
#endif
}

/** @brief Atomically checks the loop isn't already running and, if not,
 * populates the singleton. Returns false (leaving the singleton untouched)
 * if a run was already in progress. */
static bool _sys_runloop_claim(sys_event_queue_t *queue,
                               sys_runloop_init_func_t init_fn,
                               sys_runloop_event_func_t event_fn,
                               sys_runloop_poll_func_t poll_fn,
                               sys_runloop_exit_func_t exit_fn) {
  sys_mutex_lock(_sys_runloop_lock);

  if (_sys_runloop.queue != NULL) {
    sys_mutex_unlock(_sys_runloop_lock);
    return false;
  }

  _sys_runloop.queue = queue;
  _sys_runloop.init_fn = init_fn;
  _sys_runloop.event_fn = event_fn;
  _sys_runloop.poll_fn = poll_fn;
  _sys_runloop.exit_fn = exit_fn;

  sys_mutex_unlock(_sys_runloop_lock);
  return true;
}

/** @brief Starts workers 1..num_workers-1 for an already-claimed run (see
 * _sys_runloop_claim()). Doesn't need `lock`: by this point `queue` and the
 * callbacks are already fully published, and nothing but this same
 * (run()-calling) thread and the workers it's about to create ever touch
 * `workers`/`worker_ctx`. */
static void _sys_runloop_start_workers(uint8_t num_workers) {
  uint8_t num_cores = sys_thread_numcores();
  if (num_workers == 0 || num_workers > num_cores) {
    num_workers = num_cores;
  }
  if (num_workers > SYS_THREAD_CAPACITY) {
    num_workers = SYS_THREAD_CAPACITY;
  }

  _sys_runloop.workers = sys_waitgroup_init();
  if (_sys_runloop.workers == NULL) {
    return;
  }

  for (uint8_t i = 1; i < num_workers; i++) {
    if (!sys_waitgroup_add(_sys_runloop.workers, 1)) {
      continue;
    }
    _sys_runloop.worker_ctx[i].runloop = &_sys_runloop;
    _sys_runloop.worker_ctx[i].worker_number = i;
    if (!_sys_runloop_start_worker(i, &_sys_runloop.worker_ctx[i])) {
      sys_waitgroup_done(_sys_runloop.workers);
    }
  }
}

/** @brief Waits for every additional worker to exit, resets the singleton
 * back to "not running", and returns the exit value sys_runloop_shutdown()
 * was called with. */
static uint32_t _sys_runloop_deinit(void) {
  if (_sys_runloop.workers != NULL) {
    sys_waitgroup_wait(_sys_runloop.workers);
    sys_waitgroup_deinit(_sys_runloop.workers);
    _sys_runloop.workers = NULL;
  }

  uint32_t exit_value = sys_atomic_get(&_sys_runloop.exit_value);

  sys_mutex_lock(_sys_runloop_lock);
  _sys_runloop.queue = NULL;
  _sys_runloop.init_fn = NULL;
  _sys_runloop.event_fn = NULL;
  _sys_runloop.poll_fn = NULL;
  _sys_runloop.exit_fn = NULL;
  sys_mutex_unlock(_sys_runloop_lock);

  return exit_value;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Creates the mutex guarding the run loop singleton. Called once
 * from sys_init(), before any application thread can exist. */
bool _sys_runloop_module_init(void) {
  _sys_runloop_lock = sys_mutex_init();
  return _sys_runloop_lock != NULL;
}

/** @brief Releases the mutex guarding the run loop singleton. Called once
 * from sys_exit(). */
void _sys_runloop_module_exit(void) {
  if (_sys_runloop_lock != NULL) {
    sys_mutex_deinit(_sys_runloop_lock);
    _sys_runloop_lock = NULL;
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Starts the run loop and blocks until shutdown (see sys/runloop.h). */
uint32_t sys_runloop_run(uint8_t num_workers, sys_event_queue_t *queue,
                         sys_runloop_init_func_t init_fn,
                         sys_runloop_event_func_t event_fn,
                         sys_runloop_poll_func_t poll_fn,
                         sys_runloop_exit_func_t exit_fn) {
  if (queue == NULL || event_fn == NULL) {
    return 0;
  }
  if (!_sys_runloop_claim(queue, init_fn, event_fn, poll_fn, exit_fn)) {
    return 0;
  }

  sys_atomic_init(&_sys_runloop.exit_value, 0);
  sys_atomic_init(&_sys_runloop.shutdown_requested, 0);
  _sys_runloop_start_workers(num_workers);

  sys_debugf("runloop", "worker 0 starting");
  if (init_fn != NULL) {
    init_fn(0);
  }

  while (true) {
    sys_event_t event =
        sys_event_queue_timed_pop(queue, _SYS_RUNLOOP_POLL_INTERVAL_MS);
    if (poll_fn != NULL) {
      poll_fn();
    }
    if (event != NULL) {
      event_fn(event);
      continue;
    }
    if (sys_atomic_get(&_sys_runloop.shutdown_requested) &&
        sys_event_queue_empty(queue)) {
      break;
    }
  }

  if (exit_fn != NULL) {
    exit_fn(0);
  }

  sys_debugf("runloop", "worker 0 exiting");
  return _sys_runloop_deinit();
}

/** @brief Requests shutdown (see sys/runloop.h). */
void sys_runloop_shutdown(uint32_t exit_value) {
  sys_mutex_lock(_sys_runloop_lock);
  sys_event_queue_t *queue = _sys_runloop.queue;
  sys_mutex_unlock(_sys_runloop_lock);

  if (queue == NULL) {
    return;
  }

  sys_atomic_set(&_sys_runloop.exit_value, exit_value);
  sys_atomic_set(&_sys_runloop.shutdown_requested, 1);
  sys_event_queue_shutdown(queue);
}

/** @brief Posts an event to the run loop (see sys/runloop.h). */
bool sys_runloop_post(sys_event_t event) {
  sys_mutex_lock(_sys_runloop_lock);
  sys_event_queue_t *queue = _sys_runloop.queue;
  sys_mutex_unlock(_sys_runloop_lock);

  if (queue == NULL) {
    return false;
  }

  return sys_event_queue_try_push(queue, event);
}
