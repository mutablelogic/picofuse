#include "flash_pause.h"

#include <hardware/sync.h>
#include <pico/multicore.h>
#include <pico/time.h>

static volatile uint32_t _sys_pico_flash_pause_requested = 0;
static volatile uint32_t _sys_pico_flash_pause_acknowledged = 0;
static volatile uint32_t _sys_pico_flash_pause_worker_active = 0;

void _sys_pico_flash_pause_worker_enter(void) {
  __atomic_store_n(&_sys_pico_flash_pause_worker_active, 1, __ATOMIC_RELEASE);
}

void _sys_pico_flash_pause_worker_exit(void) {
  __atomic_store_n(&_sys_pico_flash_pause_worker_active, 0, __ATOMIC_RELEASE);
}

void __no_inline_not_in_flash_func(_sys_pico_flash_pause_point)(void) {
  if (__atomic_load_n(&_sys_pico_flash_pause_requested, __ATOMIC_ACQUIRE) ==
      0) {
    return;
  }

  uint32_t irq_state = save_and_disable_interrupts();
  __atomic_store_n(&_sys_pico_flash_pause_acknowledged, 1, __ATOMIC_RELEASE);
  __sev();

  while (__atomic_load_n(&_sys_pico_flash_pause_requested, __ATOMIC_ACQUIRE) !=
         0) {
    __wfe();
  }

  __atomic_store_n(&_sys_pico_flash_pause_acknowledged, 0, __ATOMIC_RELEASE);
  restore_interrupts(irq_state);
}

bool _sys_pico_flash_pause_request(uint32_t timeout_ms) {
  if (get_core_num() != 0u) {
    return false;
  }
  if (__atomic_load_n(&_sys_pico_flash_pause_worker_active, __ATOMIC_ACQUIRE) ==
      0) {
    return true;
  }

  __atomic_store_n(&_sys_pico_flash_pause_requested, 1, __ATOMIC_RELEASE);
  __sev();

  absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  while (__atomic_load_n(&_sys_pico_flash_pause_acknowledged,
                         __ATOMIC_ACQUIRE) == 0) {
    if (time_reached(deadline)) {
      __atomic_store_n(&_sys_pico_flash_pause_requested, 0, __ATOMIC_RELEASE);
      __sev();
      return false;
    }
    tight_loop_contents();
  }
  return true;
}

void _sys_pico_flash_pause_release(void) {
  __atomic_store_n(&_sys_pico_flash_pause_requested, 0, __ATOMIC_RELEASE);
  __sev();
}