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
 * @note Unlike I2C/SPI/PWM, Raspberry Pi OS's hardware watchdog
 * (`bcm2835_wdt`) is commonly already active out of the box - `/dev/watchdog`
 * and `/dev/watchdog0` exist and already counting down with no `config.txt`
 * change (`wdctl` shows its current timeout/time left). If it's ever not
 * already active, `dtparam=watchdog=on` in `config.txt` is the documented
 * way to force it on, needing a reboot to take effect.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

/**
 * @brief Default device path used by hw_watchdog_init().
 * @ingroup Watchdog
 *
 * `/dev/watchdog0`, not the legacy un-numbered `/dev/watchdog` misc device -
 * see hw_watchdog_init_device()'s own doc on the difference. Override by
 * defining `HW_WATCHDOG_DEFAULT_DEVICE` at compile time.
 */
#ifndef HW_WATCHDOG_DEFAULT_DEVICE
#define HW_WATCHDOG_DEFAULT_DEVICE "/dev/watchdog0"
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

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
 * Linux `/dev/watchdog0`, `/dev/watchdog1`, etc. (one per registered kernel
 * watchdog driver) - as opposed to `/dev/watchdog`, an older, un-numbered
 * device path kept only for backward compatibility, aliased to whichever
 * watchdog driver registered first.
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
