#include <hardware/platform_defs.h>
#include <hardware/regs/watchdog.h>
#include <hardware/watchdog.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>

#include "../../sys/pico/sync.h"

// The same critical_section_t-backed pool lock used by hw/led/private.h and
// hw/deviceio/deviceio.c - genuinely safe to serialize the singleton state
// below across both cores with, unlike most locking in this codebase.
#define _HW_WATCHDOG_LOCK() _sys_sync_pool_lock()
#define _HW_WATCHDOG_UNLOCK() _sys_sync_pool_unlock()

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_watchdog_t {
  uint32_t timeout_ms;
  uint32_t ping_interval_ms;
  uint64_t last_feed_ms;
  bool disable;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

#if PICO_RP2040
// RP2040 decrements twice per tick (errata RP2040-E1)
#define WATCHDOG_XFACTOR 2u
#else
#define WATCHDOG_XFACTOR 1u
#endif

static struct hw_watchdog_t _hw_watchdog = {0};
static sys_atomic_t _hw_watchdog_active;

// The watchdog's reason register and the SDK's scratch[4] survive resets which aren't
// a power-on reset, such as a debugger's, and the SDK sets scratch[4] each time it's
// enabled, so once it has timed out, every later boot looks like it did. So this
// scratch register is set only while it's enabled, and cleared when it's disabled:
// a timeout restarted the device only if it was still set. The SDK uses scratch[4] to
// [7]; [0] to [3] are the program's, and picofuse has this one.
#define _HW_WATCHDOG_SCRATCH 3
#define _HW_WATCHDOG_RUNNING 0x70667764u // "pfwd"

// Whether a timeout restarted the device, read once at the first init
static bool _hw_watchdog_read = false;
static bool _hw_watchdog_restarted = false;

static void _hw_watchdog_running(bool running) {
  watchdog_hw->scratch[_HW_WATCHDOG_SCRATCH] = running ? _HW_WATCHDOG_RUNNING : 0u;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

static bool _hw_watchdog_is_valid(const hw_watchdog_t *watchdog) {
  return watchdog != NULL && watchdog->timeout_ms != 0u;
}

///////////////////////////////////////////////////////////////////////////////
// MODULE HOOKS

// Called from hw_exit()
void _hw_watchdog_module_exit(void) { hw_watchdog_deinit(&_hw_watchdog); }

// Called from hw_poll()
void _hw_watchdog_poll(void) {
  if (sys_atomic_get(&_hw_watchdog_active) == 0u) {
    return;
  }

  hw_watchdog_t *watchdog = &_hw_watchdog;

  _HW_WATCHDOG_LOCK();
  bool due = false;
  if (_hw_watchdog_is_valid(watchdog) && !watchdog->disable) {
    uint64_t now_ms = sys_timestamp_ms();
    uint64_t next_feed_ms = watchdog->last_feed_ms + watchdog->ping_interval_ms;
    if (watchdog->last_feed_ms == 0u || now_ms >= next_feed_ms) {
      watchdog->last_feed_ms = now_ms;
      due = true;
    }
  }
  _HW_WATCHDOG_UNLOCK();

  if (due) {
    sys_debugf("watchdog", "ping");
    watchdog_update();
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC API

hw_watchdog_t *hw_watchdog_init(void) {
  hw_watchdog_t *watchdog = &_hw_watchdog;
  uint32_t timeout_ms;
  uint32_t ping_interval_ms;

  _HW_WATCHDOG_LOCK();

  // Whether the last reset was a timeout while it ran, which is read before anything
  // changes it, then cleared
  if (!_hw_watchdog_read) {
    _hw_watchdog_restarted =
        watchdog_caused_reboot() &&
        watchdog_hw->scratch[_HW_WATCHDOG_SCRATCH] == _HW_WATCHDOG_RUNNING;
    _hw_watchdog_running(false);
    _hw_watchdog_read = true;
  }

  // Reset any previous session before reinitializing singleton state.
  if (_hw_watchdog_is_valid(watchdog)) {
    watchdog_disable();
    _hw_watchdog_running(false);
    memset(watchdog, 0, sizeof(*watchdog));
  }

  timeout_ms = hw_watchdog_maxtimeout_ms();
  if (timeout_ms == 0u) {
    _HW_WATCHDOG_UNLOCK();
    return NULL;
  }

  ping_interval_ms = (timeout_ms * 64u) / 100u;
  if (ping_interval_ms == 0u) {
    ping_interval_ms = timeout_ms;
  }

  watchdog->timeout_ms = timeout_ms;
  watchdog->ping_interval_ms = ping_interval_ms;
  watchdog->last_feed_ms = 0u;
  watchdog->disable = true;
  sys_atomic_set(&_hw_watchdog_active, 0u);
  _HW_WATCHDOG_UNLOCK();
  return watchdog;
}

hw_watchdog_t *hw_watchdog_init_device(const char *device) {
  (void)device;
  return hw_watchdog_init();
}

void hw_watchdog_deinit(hw_watchdog_t *watchdog) {
  _HW_WATCHDOG_LOCK();

  if (!_hw_watchdog_is_valid(watchdog)) {
    _HW_WATCHDOG_UNLOCK();
    return;
  }

  watchdog_disable();
  _hw_watchdog_running(false);
  memset(watchdog, 0, sizeof(*watchdog));
  sys_atomic_set(&_hw_watchdog_active, 0u);
  _HW_WATCHDOG_UNLOCK();
}

uint32_t hw_watchdog_maxtimeout_ms(void) {
  return WATCHDOG_LOAD_BITS / (1000u * WATCHDOG_XFACTOR);
}

bool hw_watchdog_did_reset(hw_watchdog_t *watchdog) {
  bool did_reset;

  _HW_WATCHDOG_LOCK();
  if (!_hw_watchdog_is_valid(watchdog)) {
    _HW_WATCHDOG_UNLOCK();
    return false;
  }

  did_reset = _hw_watchdog_restarted;
  _HW_WATCHDOG_UNLOCK();
  return did_reset;
}

void hw_watchdog_enable(hw_watchdog_t *watchdog, bool enable) {
  _HW_WATCHDOG_LOCK();

  if (!_hw_watchdog_is_valid(watchdog)) {
    _HW_WATCHDOG_UNLOCK();
    return;
  }

  if (enable) {
    watchdog_enable(watchdog->timeout_ms * WATCHDOG_XFACTOR, true);
    _hw_watchdog_running(true);
    watchdog->disable = false;
    watchdog->last_feed_ms = 0u;
    sys_atomic_set(&_hw_watchdog_active, 1u);
  } else {
    watchdog_disable();
    _hw_watchdog_running(false);
    watchdog->disable = true;
    sys_atomic_set(&_hw_watchdog_active, 0u);
  }

  _HW_WATCHDOG_UNLOCK();
}

void hw_watchdog_reset(hw_watchdog_t *watchdog, uint32_t delay_ms) {
  uint32_t max_timeout_ms;

  _HW_WATCHDOG_LOCK();
  if (!_hw_watchdog_is_valid(watchdog) || delay_ms == 0u) {
    _HW_WATCHDOG_UNLOCK();
    return;
  }

  max_timeout_ms = hw_watchdog_maxtimeout_ms();
  if (delay_ms > max_timeout_ms) {
    delay_ms = max_timeout_ms;
  }

  // The watchdog causes this reset too, as it does on Linux
  watchdog_enable(delay_ms * WATCHDOG_XFACTOR, true);
  _hw_watchdog_running(true);
  watchdog->disable = true;
  sys_atomic_set(&_hw_watchdog_active, 0u);
  _HW_WATCHDOG_UNLOCK();
}
