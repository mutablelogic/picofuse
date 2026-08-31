#include "sync.h"
#include <pico/critical_section.h>

// A single critical section shared by every pool-backed sync primitive
// (mutex, condition variable, wait-group) and by event queues' ring-buffer
// bookkeeping (see sys/event/lock.c). RP2040/RP2350 only have a handful of
// hardware spin locks, and each of these only ever holds this lock for a
// few array-bookkeeping instructions, so sharing one lock across all of
// them costs far less than dedicating a spin lock to each.
static critical_section_t _sys_sync_crit_sec;

void _sys_sync_module_init(void) {
  critical_section_init(&_sys_sync_crit_sec);
}

void _sys_sync_module_deinit(void) {
  critical_section_deinit(&_sys_sync_crit_sec);
}

void _sys_sync_pool_lock(void) {
  critical_section_enter_blocking(&_sys_sync_crit_sec);
}

void _sys_sync_pool_unlock(void) {
  critical_section_exit(&_sys_sync_crit_sec);
}
