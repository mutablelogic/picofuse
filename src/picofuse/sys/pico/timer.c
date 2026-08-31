#include <hardware/platform_defs.h>
#include <pico/critical_section.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <picofuse/sys.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_timer_t {
  void (*callback)(sys_timer_t *);
  uint32_t interval_ms;
  void *userdata;
  repeating_timer_t repeating_timer;
  uint8_t callback_core;
  bool init;
  bool running;
  bool callback_active;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static critical_section_t _sys_timer_pool_lock;
static sys_timer_t _sys_timer_pool[SYS_TIMER_CAPACITY];
static size_t _sys_timer_pool_index = 0;

// One alarm pool per core, created lazily (see sys_timer_start()) the first
// time a timer is started from that core.
static alarm_pool_t *_sys_timer_pools[NUM_CORES];

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

static bool _sys_timer_callback(repeating_timer_t *rt) {
  sys_timer_t *timer = (sys_timer_t *)rt->user_data;
  if (timer == NULL) {
    return false;
  }

  void (*callback)(sys_timer_t *) = NULL;
  critical_section_enter_blocking(&_sys_timer_pool_lock);
  if (timer->running && timer->callback != NULL && !timer->callback_active) {
    timer->callback_active = true;
    timer->callback_core = (uint8_t)get_core_num();
    callback = timer->callback;
  }
  critical_section_exit(&_sys_timer_pool_lock);

  if (callback != NULL) {
    callback(timer);
  }

  critical_section_enter_blocking(&_sys_timer_pool_lock);
  if (callback != NULL) {
    timer->callback_active = false;
  }
  bool keep_running = timer->running;
  critical_section_exit(&_sys_timer_pool_lock);

  return keep_running;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE (module)

/** @brief Initializes the critical section guarding the timer pool. */
void _sys_timer_module_init(void) {
  critical_section_init(&_sys_timer_pool_lock);
}

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

  critical_section_enter_blocking(&_sys_timer_pool_lock);

  for (size_t offset = 0; offset < SYS_TIMER_CAPACITY; offset++) {
    size_t index = (_sys_timer_pool_index + offset) % SYS_TIMER_CAPACITY;
    sys_timer_t *timer = &_sys_timer_pool[index];
    if (timer->init || timer->callback_active) {
      continue;
    }

    timer->callback = callback;
    timer->interval_ms = interval_ms;
    timer->userdata = userdata;
    timer->running = false;
    timer->callback_active = false;
    timer->init = true;

    _sys_timer_pool_index = (index + 1) % SYS_TIMER_CAPACITY;
    critical_section_exit(&_sys_timer_pool_lock);
    return timer;
  }

  critical_section_exit(&_sys_timer_pool_lock);
  return NULL;
}

/** @brief Stops a timer and releases it back to the pool. */
void sys_timer_deinit(sys_timer_t *timer) {
  if (timer == NULL) {
    return;
  }

  // A timer's callback runs as an alarm IRQ on whichever core's pool it was
  // armed on. __get_current_exception() alone can't tell "my own callback"
  // apart from some unrelated IRQ (including a *different* timer's callback
  // on the same or other core), so track the specific core that's actually
  // running *this* timer's callback, mirroring how darwin/linux compare
  // against a saved thread identity.
  critical_section_enter_blocking(&_sys_timer_pool_lock);
  bool inited = timer->init;
  bool in_callback = timer->callback_active &&
                     timer->callback_core == (uint8_t)get_core_num();
  critical_section_exit(&_sys_timer_pool_lock);
  if (!inited) {
    return;
  }

  bool was_running = false;
  critical_section_enter_blocking(&_sys_timer_pool_lock);
  if (timer->running) {
    was_running = true;
    timer->running = false;
  }
  critical_section_exit(&_sys_timer_pool_lock);

  if (was_running) {
    cancel_repeating_timer(&timer->repeating_timer);
  }

  if (!in_callback) {
    while (true) {
      critical_section_enter_blocking(&_sys_timer_pool_lock);
      bool active = timer->callback_active;
      critical_section_exit(&_sys_timer_pool_lock);

      if (!active) {
        break;
      }

      sleep_ms(1u);
    }
  }

  critical_section_enter_blocking(&_sys_timer_pool_lock);
  timer->init = false;
  if (!in_callback) {
    timer->callback_active = false;
  }
  critical_section_exit(&_sys_timer_pool_lock);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Starts a configured timer. */
bool sys_timer_start(sys_timer_t *timer) {
  if (timer == NULL) {
    return false;
  }

  critical_section_enter_blocking(&_sys_timer_pool_lock);
  bool can_start = timer->init && !timer->running;
  critical_section_exit(&_sys_timer_pool_lock);
  if (!can_start) {
    return false;
  }

  // Each core gets its own alarm pool, created (and thus core-affine) the
  // first time a timer is started from it: an alarm pool always delivers
  // its callbacks on the core that created it, so lazily creating one per
  // calling core is what makes a timer's callback land on the same core
  // that started it, matching this module's documented contract. Guarded
  // by the pool lock since alarm_pool_create() hard-asserts if the same
  // hardware alarm is claimed twice.
  uint core = get_core_num();
  critical_section_enter_blocking(&_sys_timer_pool_lock);
  alarm_pool_t *pool = _sys_timer_pools[core];
  if (pool == NULL) {
    pool = alarm_pool_create(core, SYS_TIMER_CAPACITY);
    _sys_timer_pools[core] = pool;
  }
  critical_section_exit(&_sys_timer_pool_lock);
  if (pool == NULL) {
    return false;
  }

  critical_section_enter_blocking(&_sys_timer_pool_lock);
  timer->running = true;
  critical_section_exit(&_sys_timer_pool_lock);

  if (!alarm_pool_add_repeating_timer_ms(pool, (int32_t)timer->interval_ms,
                                         _sys_timer_callback, timer,
                                         &timer->repeating_timer)) {
    critical_section_enter_blocking(&_sys_timer_pool_lock);
    timer->running = false;
    critical_section_exit(&_sys_timer_pool_lock);
    return false;
  }

  return true;
}

/** @brief Returns the userdata pointer associated with a timer. */
void *sys_timer_userdata(sys_timer_t *timer) {
  if (timer == NULL) {
    return NULL;
  }

  critical_section_enter_blocking(&_sys_timer_pool_lock);
  void *userdata = timer->init ? timer->userdata : NULL;
  critical_section_exit(&_sys_timer_pool_lock);
  return userdata;
}
