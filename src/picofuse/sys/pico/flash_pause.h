#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Mark the current Pico runloop worker as available for flash pause.
 *
 * @todo Only covers core 1 once a runloop worker has actually called this -
 * not while it's parked in `_sys_thread_wrapper()`'s own
 * `multicore_fifo_pop_blocking()` (`sys/pico/thread.c`), before the FIFO
 * handshake right after `multicore_launch_core1()`/
 * `sys_thread_create_on_core()` completes, or again between one runloop
 * session's own `_sys_pico_flash_pause_worker_exit()` and a later one's
 * call to this. Low severity in practice: the window is a hardware FIFO
 * round-trip (microseconds), the realistic failure mode is a transient
 * bus stall rather than corruption, and `hw_block_flash_init()` always
 * allocates storage after `__flash_binary_end` - a different flash region
 * than whatever core 1 would actually be fetching from at that moment.
 * Closing it properly would mean `_sys_thread_wrapper()` itself marking
 * core 1 pausable for its own `multicore_fifo_pop_blocking()` wait, not
 * just the runloop worker built on top of it.
 */
void _sys_pico_flash_pause_worker_enter(void);

/** @brief Mark the current Pico runloop worker as unavailable for flash pause.
 */
void _sys_pico_flash_pause_worker_exit(void);

/** @brief Park core 1 in RAM while a core-0 flash operation is active. */
void _sys_pico_flash_pause_point(void);

/** @brief Request core 1 to reach its next pause point within @p timeout_ms. */
bool _sys_pico_flash_pause_request(uint32_t timeout_ms);

/** @brief Resume core 1 after a successful pause request. */
void _sys_pico_flash_pause_release(void);