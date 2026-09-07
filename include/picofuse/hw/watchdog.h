/**
 * @file watchdog.h
 * @brief Hardware watchdog timer abstraction layer.
 * @defgroup Watchdog Watchdog
 * @ingroup Hardware
 * @details
 * The watchdog module provides a singleton watchdog adapter that can detect a
 * stalled application and can also trigger a delayed reset programmatically.
 *
 * After enabling the watchdog, applications should call `hw_poll()` regularly
 * so the watchdog can be fed. If feeding stops for longer than the supported
 * timeout, the backend triggers a reset action.
 *
 * `hw_watchdog_reset()` arms a delayed device reset (a reboot) without
 * requiring additional polling. The pending reset can be cancelled by
 * calling `hw_watchdog_enable()`.
 *
 * Backends may provide internal synchronization so watchdog control calls can
 * be made safely from multiple threads. The Pico backend serializes watchdog
 * state and hardware access with a critical section.
 *
 * Watchdog APIs should not be called from IRQ handlers unless a backend
 * explicitly documents IRQ-safe usage.
 *
 * Backends may map the reset action to native watchdog hardware or to a
 * host-side termination path when no hardware watchdog is available.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Watchdog adapter handle.
 * @ingroup Watchdog
 * @headerfile watchdog.h picofuse/hw.h
 */
typedef struct hw_watchdog_t hw_watchdog_t;

/**
 * @brief Initialize the watchdog singleton.
 * @ingroup Watchdog
 * @return Singleton watchdog handle, or `NULL` when unsupported.
 */
hw_watchdog_t *hw_watchdog_init(void);

/**
 * @brief Initialize the watchdog singleton for a specific device path.
 * @ingroup Watchdog
 * @param device Device path or identifier to open.
 * @return Singleton watchdog handle, or `NULL` when unsupported.
 *
 * This is intended for backends that expose multiple watchdog devices, such as
 * Linux `/dev/watchdog*` nodes.
 */
hw_watchdog_t *hw_watchdog_init_device(const char *device);

/**
 * @brief Deinitialize the watchdog singleton.
 * @ingroup Watchdog
 * @param watchdog Watchdog handle.
 *
 * Deinitialization also disables any active watchdog feeding or pending reset
 * behavior before releasing backend resources.
 */
void hw_watchdog_deinit(hw_watchdog_t *watchdog);

/**
 * @brief Return the maximum supported watchdog timeout in milliseconds.
 * @ingroup Watchdog
 * @return Maximum supported timeout in milliseconds, or `0` when unsupported.
 */
uint32_t hw_watchdog_maxtimeout_ms(void);

/**
 * @brief Report whether the last reboot/reset was caused by the watchdog.
 * @ingroup Watchdog
 * @param watchdog Watchdog handle.
 * @retval true Previous reboot/reset was watchdog-driven.
 * @retval false Previous reboot/reset was not watchdog-driven or unsupported.
 */
bool hw_watchdog_did_reset(hw_watchdog_t *watchdog);

/**
 * @brief Enable or disable watchdog feeding mode.
 * @ingroup Watchdog
 * @param watchdog Watchdog handle.
 * @param enable When `true`, enable watchdog feeding mode. When `false`,
 * disable watchdog activity.
 *
 * Enabling watchdog feeding mode expects callers to invoke `hw_poll()` often
 * enough to refresh the watchdog before timeout.
 */
void hw_watchdog_enable(hw_watchdog_t *watchdog, bool enable);

/**
 * @brief Trigger a delayed device reset (reboot).
 * @ingroup Watchdog
 * @param watchdog Watchdog handle.
 * @param delay_ms Delay in milliseconds before the device resets.
 *
 * The delay is clamped to `hw_watchdog_maxtimeout_ms()`. A pending reset can be
 * cancelled by calling `hw_watchdog_enable()`.
 */
void hw_watchdog_reset(hw_watchdog_t *watchdog, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif
