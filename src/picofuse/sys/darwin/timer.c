#include <dispatch/dispatch.h>
#include <picofuse/sys.h>
#include <pthread.h>
#include <stddef.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_timer_t {
  void (*callback)(sys_timer_t *);
  uint32_t interval_ms;
  void *userdata;
  dispatch_source_t source;
  pthread_t callback_thread;
  bool callback_active;
  bool init;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static pthread_mutex_t _sys_timer_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static sys_timer_t _sys_timer_pool[SYS_TIMER_CAPACITY] = {0};
static size_t _sys_timer_pool_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Returns true when a timer handle is initialized. */
static inline bool _sys_timer_valid(sys_timer_t *timer) {
  return timer != NULL && timer->init;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

static void _sys_timer_callback(void *context) {
  sys_timer_t *timer = (sys_timer_t *)context;
  if (timer == NULL) {
    return;
  }

  pthread_mutex_lock(&_sys_timer_pool_lock);
  if (timer->init && timer->source != NULL && !timer->callback_active) {
    timer->callback_active = true;
    timer->callback_thread = pthread_self();
  }
  void (*callback)(sys_timer_t *) =
      timer->callback_active ? timer->callback : NULL;
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

    usleep(1000);
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
    timer->source = NULL;
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
  if (!_sys_timer_valid(timer)) {
    return;
  }

  bool in_callback = _sys_timer_callback_in_progress_for_current_thread(timer);

  if (timer->source != NULL) {
    dispatch_source_cancel(timer->source);
    dispatch_release(timer->source);
    timer->source = NULL;
  }

  if (!in_callback) {
    _sys_timer_wait_for_callback(timer);
  }

  pthread_mutex_lock(&_sys_timer_pool_lock);
  timer->init = false;
  if (!in_callback) {
    timer->callback_active = false;
  }
  pthread_mutex_unlock(&_sys_timer_pool_lock);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Starts a configured timer. */
bool sys_timer_start(sys_timer_t *timer) {
  if (!_sys_timer_valid(timer) || timer->source != NULL) {
    return false;
  }

  dispatch_queue_t queue =
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
  dispatch_source_t source =
      dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
  if (source == NULL) {
    return false;
  }

  int64_t interval_ns = (int64_t)timer->interval_ms * 1000000LL;
  dispatch_source_set_timer(source,
                            dispatch_time(DISPATCH_TIME_NOW, interval_ns),
                            (uint64_t)interval_ns, 0);
  dispatch_set_context(source, timer);
  dispatch_source_set_event_handler_f(source, _sys_timer_callback);

  timer->source = source;
  dispatch_resume(source);
  return true;
}

/** @brief Returns the userdata pointer associated with a timer. */
void *sys_timer_userdata(sys_timer_t *timer) {
  if (!_sys_timer_valid(timer)) {
    return NULL;
  }
  return timer->userdata;
}
