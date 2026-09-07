#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief Mark the current Pico runloop worker as available for flash pause. */
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