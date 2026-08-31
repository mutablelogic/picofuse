#define _POSIX_C_SOURCE 199309L

#include <picofuse/sys.h>

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_timer_t {
  void (*callback)(sys_timer_t *);
  uint32_t interval_ms;
  void *userdata;
  timer_t timer_id;
  pthread_t callback_thread;
  bool init;
  bool running;
  bool callback_active;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static pthread_mutex_t _sys_timer_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static sys_timer_t _sys_timer_pool[SYS_TIMER_CAPACITY] = {0};
static size_t _sys_timer_pool_index = 0;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

static void _sys_timer_callback(union sigval sv) {
  sys_timer_t *timer = (sys_timer_t *)sv.sival_ptr;
  if (timer == NULL) {
    return;
  }

  void (*callback)(sys_timer_t *) = NULL;
  pthread_mutex_lock(&_sys_timer_pool_lock);
  // SIGEV_THREAD may dispatch overlapping notifications if callback execution
  // exceeds interval. Serialize per timer to keep one-shot deinit semantics
  // deterministic across platforms.
  if (timer->init && timer->running && !timer->callback_active) {
    timer->callback_active = true;
    timer->callback_thread = pthread_self();
    callback = timer->callback;
  }
  pthread_mutex_unlock(&_sys_timer_pool_lock);

  if (callback != NULL) {
    callback(timer);

    pthread_mutex_lock(&_sys_timer_pool_lock);
    timer->callback_active = false;
    pthread_mutex_unlock(&_sys_timer_pool_lock);
  }
}

static bool
_sys_timer_callback_in_progress_for_current_thread(sys_timer_t *timer) {
  if (timer == NULL) {
    return false;
  }

  pthread_mutex_lock(&_sys_timer_pool_lock);
  bool in_progress = timer->callback_active &&
                     pthread_equal(pthread_self(), timer->callback_thread);
  pthread_mutex_unlock(&_sys_timer_pool_lock);
  return in_progress;
}

static void _sys_timer_wait_for_callback(sys_timer_t *timer) {
  if (timer == NULL) {
    return;
  }

  while (true) {
    pthread_mutex_lock(&_sys_timer_pool_lock);
    bool active = timer->callback_active;
    pthread_mutex_unlock(&_sys_timer_pool_lock);

    if (!active) {
      return;
    }

    struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000L};
    nanosleep(&delay, NULL);
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE (module)

/** @brief Stops and releases every pool timer. Called once at sys_exit(). */
void _sys_timer_module_exit(void) {
  for (size_t i = 0; i < SYS_TIMER_CAPACITY; i++) {
    sys_timer_deinit(&_sys_timer_pool[i]);
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Allocates and configures a timer from the static pool. */
sys_timer_t *sys_timer_init(uint32_t interval_ms,
                            void (*callback)(sys_timer_t *), void *userdata) {
  if (interval_ms == 0 || callback == NULL) {
    return NULL;
  }

  pthread_mutex_lock(&_sys_timer_pool_lock);

  for (size_t offset = 0; offset < SYS_TIMER_CAPACITY; offset++) {
    size_t index = (_sys_timer_pool_index + offset) % SYS_TIMER_CAPACITY;
    sys_timer_t *timer = &_sys_timer_pool[index];
    if (timer->init || timer->callback_active) {
      continue;
    }

    timer->callback = callback;
    timer->interval_ms = interval_ms;
    timer->userdata = userdata;
    timer->timer_id = (timer_t)0;
    timer->running = false;
    timer->callback_active = false;
    timer->init = true;

    _sys_timer_pool_index = (index + 1) % SYS_TIMER_CAPACITY;
    pthread_mutex_unlock(&_sys_timer_pool_lock);
    return timer;
  }

  pthread_mutex_unlock(&_sys_timer_pool_lock);
  return NULL;
}

/** @brief Stops a timer and releases it back to the pool. */
void sys_timer_deinit(sys_timer_t *timer) {
  if (timer == NULL) {
    return;
  }

  timer_t timer_id = (timer_t)0;
  bool in_callback = _sys_timer_callback_in_progress_for_current_thread(timer);

  pthread_mutex_lock(&_sys_timer_pool_lock);
  if (timer->init && timer->running) {
    timer_id = timer->timer_id;
  }

  timer->running = false;
  timer->init = false;
  timer->timer_id = (timer_t)0;
  pthread_mutex_unlock(&_sys_timer_pool_lock);

  if (timer_id != (timer_t)0) {
    timer_delete(timer_id);
  }

  if (!in_callback) {
    _sys_timer_wait_for_callback(timer);
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Starts a configured timer. */
bool sys_timer_start(sys_timer_t *timer) {
  if (timer == NULL) {
    return false;
  }

  pthread_mutex_lock(&_sys_timer_pool_lock);
  if (!timer->init || timer->running) {
    pthread_mutex_unlock(&_sys_timer_pool_lock);
    return false;
  }

  struct sigevent event = {
      .sigev_notify = SIGEV_THREAD,
      .sigev_value.sival_ptr = timer,
      .sigev_notify_function = _sys_timer_callback,
  };

  timer_t timer_id = (timer_t)0;
  if (timer_create(CLOCK_MONOTONIC, &event, &timer_id) != 0) {
    pthread_mutex_unlock(&_sys_timer_pool_lock);
    return false;
  }

  struct itimerspec timer_spec = {
      .it_interval = {.tv_sec = timer->interval_ms / 1000,
                      .tv_nsec = (long)(timer->interval_ms % 1000) * 1000000L},
      .it_value = {.tv_sec = timer->interval_ms / 1000,
                   .tv_nsec = (long)(timer->interval_ms % 1000) * 1000000L},
  };

  // Set running before arming so an immediate SIGEV_THREAD callback is not
  // dropped while observing timer state.
  timer->timer_id = timer_id;
  timer->running = true;

  if (timer_settime(timer_id, 0, &timer_spec, NULL) != 0) {
    timer->running = false;
    timer->timer_id = (timer_t)0;
    pthread_mutex_unlock(&_sys_timer_pool_lock);
    timer_delete(timer_id);
    return false;
  }

  pthread_mutex_unlock(&_sys_timer_pool_lock);
  return true;
}

/** @brief Returns the userdata pointer associated with a timer. */
void *sys_timer_userdata(sys_timer_t *timer) {
  if (timer == NULL) {
    return NULL;
  }

  pthread_mutex_lock(&_sys_timer_pool_lock);
  void *userdata = timer->init ? timer->userdata : NULL;
  pthread_mutex_unlock(&_sys_timer_pool_lock);
  return userdata;
}
