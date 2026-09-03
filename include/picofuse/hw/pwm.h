/**
 * @file pwm.h
 * @brief PWM (Pulse Width Modulation) interface
 * @defgroup PWM PWM
 * @ingroup Hardware
 *
 * Pulse Width Modulation (PWM) interface for hardware platforms.
 *
 * PWM is a digital waveform technique that approximates an analog level by
 * rapidly toggling an output between low and high. Each PWM cycle has:
 * - A period: total cycle duration.
 * - A duty cycle: percentage of that period spent high.
 *
 * For example, 1 kHz PWM with 25% duty is high for 250 us and low for 750 us
 * every cycle. Common uses include LED dimming, motor speed control, and tone
 * generation.
 *
 * This module provides functions to initialize PWM outputs, configure period
 * and duty cycle, control output state, and optionally receive wrap callbacks
 * at period boundaries.
 *
 * @note On Raspberry Pi OS
 * PWM outputs are disabled by default and must be enabled in `config.txt`
 * with a device tree overlay, for example `dtoverlay=pwm,pin=18,func=2` for
 * a single channel on GPIO18, or
 * `dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2` for both channels
 * (GPIO18/GPIO19); see `/boot/firmware/overlays/README` for the full pin/
 * function table, other pins, and other overlays. Once loaded, the kernel
 * exposes each channel under `/sys/class/pwm/pwmchipN/` rather than a
 * fixed device path.
 * @par
 * @note Pico backend details (RP2040/RP2350):
 * The hardware groups GPIOs into PWM slices. Each slice has two output
 * channels (A and B) that share the same period (wrap) and divider settings
 * but have independent duty levels. Because period settings are shared per
 * slice, changing period on one channel affects the other channel in the same
 * slice.
 */
#pragma once
#include "gpio.h"
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque PWM handle.
 * @ingroup PWM
 * @headerfile pwm.h hw/hw.h
 */
typedef struct hw_pwm_t hw_pwm_t;

/**
 * @brief PWM configuration.
 * @ingroup PWM
 *
 * When `NULL` is passed to @ref hw_pwm_init, backend defaults are used.
 */
typedef struct {
  uint64_t period_ns; ///< Requested PWM period in nanoseconds.
  float duty_percent; ///< Requested duty cycle percentage in [0.0, 100.0].
  bool enabled;       ///< Initial output enable state.
} hw_pwm_config_t;

/**
 * @brief PWM wrap callback function pointer.
 * @ingroup PWM
 *
 * Backends that support PWM wrap interrupts can invoke this callback on each
 * counter wrap event, when the PWM counter rolls from its top value back to
 * zero (the boundary between PWM periods).
 */
typedef void (*hw_pwm_callback_t)(hw_pwm_t *pwm, void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a PWM output on a GPIO pin.
 * @ingroup PWM
 * @param gpio GPIO handle for a PWM-capable pin.
 * @param callback Optional callback invoked on PWM counter wrap (top -> 0)
 * events, typically once per completed PWM period.
 * @param userdata User context pointer forwarded to @p callback.
 * @param config Optional PWM configuration. Pass `NULL` to use defaults.
 * @return PWM handle or NULL on failure. If @p callback is not `NULL`,
 * backends that do not support wrap interrupt callbacks return `NULL`.
 */
hw_pwm_t *hw_pwm_init(hw_gpio_t *gpio, hw_pwm_callback_t callback,
                      void *userdata, const hw_pwm_config_t *config);

/**
 * @brief Initialize a PWM output from a platform-specific device path.
 * @ingroup PWM
 * @param device Device identifier such as `/sys/class/pwm/pwmchip0`.
 * @param channel Channel index within @p device, e.g. `0` for `pwm0`. A
 * chip may expose more than one - see its `npwm` file for how many.
 * @param config Optional PWM configuration. Pass `NULL` to use defaults.
 * @return PWM handle or NULL on failure. Release it with hw_pwm_deinit().
 *
 * This entry point is intended for platforms where PWM channels are
 * exposed as named sysfs paths rather than a fixed set of slices/channels
 * reachable via a GPIO handle. There is no @p callback parameter here -
 * unlike hw_pwm_init(), this entry point has no wrap-interrupt mechanism
 * to offer at all (see hw_pwm_irq_supported()), not merely an unsupported
 * one.
 */
hw_pwm_t *hw_pwm_init_device(const char *device, uint8_t channel,
                             const hw_pwm_config_t *config);

/**
 * @brief Deinitialize a PWM handle.
 * @ingroup PWM
 * @param pwm PWM handle.
 */
void hw_pwm_deinit(hw_pwm_t *pwm);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// CONFIGURATION

/** @name Configuration
 * @{ */

/**
 * @brief Set the PWM period.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @param period_ns PWM period in nanoseconds.
 * @retval true Period was accepted.
 * @retval false Period is unsupported or the handle is invalid.
 */
bool hw_pwm_set_period_ns(hw_pwm_t *pwm, uint64_t period_ns);

/**
 * @brief Get the configured PWM period.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @return PWM period in nanoseconds, or 0 on invalid handle.
 */
uint64_t hw_pwm_get_period_ns(const hw_pwm_t *pwm);

/**
 * @brief Set duty cycle percentage.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @param duty_percent Duty cycle percentage in [0.0, 100.0]. Values outside
 * this range are clamped by the backend.
 * @retval true Duty cycle was applied.
 * @retval false The handle is invalid or duty control is unsupported.
 */
bool hw_pwm_set_duty_percent(hw_pwm_t *pwm, float duty_percent);

/**
 * @brief Get duty cycle percentage.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @return Duty cycle in [0.0, 100.0], or 0.0 on invalid handle.
 */
float hw_pwm_get_duty_percent(const hw_pwm_t *pwm);

/**
 * @brief Apply a complete PWM configuration.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @param config PWM configuration to apply.
 * @retval true Configuration was applied.
 * @retval false Configuration failed.
 */
bool hw_pwm_set_config(hw_pwm_t *pwm, const hw_pwm_config_t *config);

/**
 * @brief Read current PWM configuration.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @param out_config Output destination for current configuration.
 * @retval true Configuration was written to @p out_config.
 * @retval false Handle or output pointer is invalid.
 */
bool hw_pwm_get_config(const hw_pwm_t *pwm, hw_pwm_config_t *out_config);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// CONTROL

/** @name Control
 * @{ */

/**
 * @brief Enable or disable PWM output.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @param enabled `true` to enable output, `false` to disable.
 */
void hw_pwm_set_enabled(hw_pwm_t *pwm, bool enabled);

/**
 * @brief Query whether PWM output is enabled.
 * @ingroup PWM
 * @param pwm PWM handle.
 * @retval true PWM output is enabled.
 * @retval false PWM output is disabled or handle is invalid.
 */
bool hw_pwm_get_enabled(const hw_pwm_t *pwm);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// INTERRUPTS

/** @name Interrupts
 * @{ */

/**
 * @brief Check whether PWM wrap interrupt callbacks are supported.
 * @ingroup PWM
 * @retval true Wrap interrupts are supported on this platform.
 * @retval false Wrap interrupts are unsupported.
 */
bool hw_pwm_irq_supported(void);

/** @} */
