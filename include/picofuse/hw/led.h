/**
 * @file led.h
 * @brief Support for controlling on-board and external LEDs.
 * @defgroup LED LED
 * @ingroup Hardware
 *
 * Support for controlling on-board and external LEDs.
 *
 * One API - hw_led_set(), hw_led_set_brightness(), hw_led_clear(),
 * hw_led_blink() - works the same way no matter what's actually behind
 * the handle: a Wi-Fi chip GPIO, a plain GPIO pin, a PWM output, a
 * NeoPixel/WS2812 chain, or (on Linux) a kernel LED-class device.
 * hw_led_init_default() finds and initializes whichever of these the
 * current board actually has, so most code never needs to know which
 * one it got.
 *
 * @code
 * hw_led_t *led = hw_led_init_default();
 * if (led != NULL) {
 *   hw_led_set(led, 0, true);             // on
 *   hw_led_set_brightness(led, 0, 50.0f); // 50%, where supported
 *   hw_led_clear(led);                    // off
 * }
 * @endcode
 *
 * hw_led_blink() runs a blink on a timer in the background, repeating
 * until explicitly stopped or just once:
 *
 * @code
 * // Repeating: blink at 2Hz (250ms on, 250ms off) while, say, Wi-Fi is
 * // still connecting.
 * hw_led_blink(led, 0, 250, true);
 * ...
 * // Outcome known - hw_led_set()/hw_led_clear() cancel the blink, same
 * // as calling hw_led_blink() again would.
 * hw_led_set(led, 0, true); // connected: solid on
 *
 * // Non-repeating: a single "flash" - the LED turns on once, after one
 * // period_ms, and stays on until something else changes it.
 * hw_led_blink(led, 0, 500, false);
 * @endcode
 *
 * Only one blink can be active per handle at a time - see
 * hw_led_blink()'s own doc, notably a real limitation for NeoPixel,
 * whose whole chain shares one handle.
 */
#pragma once
#include "gpio.h"
#include "pwm.h"
#include <picofuse/pix/color.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

/**
 * @brief Value returned when no default board LED GPIO is available.
 * @ingroup LED
 */
#define HW_LED_GPIO_NONE 0xFFu

/**
 * @brief Capacity of the LED handle pool.
 * @ingroup LED
 *
 * Override by defining `HW_LED_POOL_CAPACITY` at compile time.
 */
#ifndef HW_LED_POOL_CAPACITY
#define HW_LED_POOL_CAPACITY 8u
#endif

/**
 * @brief Size in bytes of the per-handle scratch context space embedded in
 * every hw_led_t, for a backend's own private per-LED state - a GPIO or
 * PWM handle pointer, a NeoPixel chain's length/buffer pointer, a sysfs
 * LED path pointer, and similar. Variable-length state (a sysfs path, a
 * NeoPixel color buffer) is heap-allocated by the backend and only its
 * pointer stored here, so this only needs to be big enough for a handful
 * of pointer/scalar fields, not whatever the largest backend's data
 * happens to be.
 * @ingroup LED
 *
 * Override by defining `HW_LED_CONTEXT_SIZE` at compile time.
 */
#ifndef HW_LED_CONTEXT_SIZE
#define HW_LED_CONTEXT_SIZE 32
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Default board LED access type.
 * @ingroup LED
 */
typedef enum {
  hw_led_type_none = 0, ///< No default board LED is available.
  hw_led_type_wifi,     ///< LED is controlled through CYW43 Wi-Fi GPIO.
  hw_led_type_neopixel, ///< LED is a WS2812/NeoPixel data pin.
  hw_led_type_gpio,     ///< LED is a directly controlled GPIO pin.
  hw_led_type_pwm,      ///< LED is controlled through PWM on a GPIO pin.
} hw_led_type_t;

/**
 * @brief Opaque LED handle.
 * @ingroup LED
 * @headerfile led.h hw/hw.h
 */
typedef struct hw_led_t hw_led_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a direct GPIO LED.
 * @ingroup LED
 * @param gpio GPIO handle for the LED pin.
 * @return LED handle, or `NULL` when unsupported or invalid.
 */
hw_led_t *hw_led_init_gpio(hw_gpio_t *gpio);

/**
 * @brief Initialize a NeoPixel/WS2812 LED data pin.
 * @ingroup LED
 * @param gpio GPIO handle for the NeoPixel data pin.
 * @param led_count Number of NeoPixels in the daisy chain.
 * @return LED handle, or `NULL` when unsupported or invalid.
 */
hw_led_t *hw_led_init_neopixel(hw_gpio_t *gpio, uint8_t led_count);

/**
 * @brief Initialize a Wi-Fi controlled LED.
 * @ingroup LED
 * @return LED handle when CYW43 support is available, otherwise `NULL`.
 */
hw_led_t *hw_led_init_wifi(void);

/**
 * @brief Initialize a PWM controlled LED.
 * @ingroup LED
 * @param pwm PWM handle for the LED.
 *
 * The PWM output is forced to an off state during initialization.
 * @return LED handle, or `NULL` when unsupported or invalid.
 */
hw_led_t *hw_led_init_pwm(hw_pwm_t *pwm);

/**
 * @brief Initialize an LED from a platform-specific device path or name.
 * @ingroup LED
 * @param name LED identifier, e.g. `"led0"` for `/sys/class/leds/led0` on
 * Linux.
 * @return LED handle, or `NULL` when unsupported or invalid.
 *
 * This entry point is intended for platforms where LEDs are bound to a
 * kernel driver and exposed by name rather than reachable as a raw GPIO -
 * on Linux/Raspberry Pi, the on-board activity LED is owned by the LED
 * class subsystem (`/sys/class/leds/`), not a GPIO userspace can toggle
 * directly. Unsupported elsewhere.
 */
hw_led_t *hw_led_init_device(const char *name);

/**
 * @brief Initialize the default on-board LED.
 * @ingroup LED
 *
 * The backend detects the default LED type and initializes the corresponding
 * LED path automatically.
 *
 * @return LED handle, or `NULL` when no default on-board LED is available or
 * initialization fails.
 */
hw_led_t *hw_led_init_default(void);

/**
 * @brief Deinitialize an LED handle.
 * @ingroup LED
 * @param led LED handle.
 */
void hw_led_deinit(hw_led_t *led);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Return the default board LED's control pin.
 * @ingroup LED
 * @param out_type Optional destination for detected LED type.
 * @param out_count Optional destination for LED count. Defaults to 1 for
 * available LEDs, or 0 when no default LED is available.
 * @return The default board LED's control pin, or @ref HW_LED_GPIO_NONE
 * when no default on-board LED is available. For @ref hw_led_type_wifi,
 * this is a CYW43 Wi-Fi-chip GPIO index (e.g. `CYW43_WL_GPIO_LED_PIN`),
 * not an RP2040 board pin - it isn't valid to pass to hw_gpio_init() or
 * any other GPIO API, only to hw_led_init_wifi()'s own internals.
 */
uint8_t hw_led_gpio_default(hw_led_type_t *out_type, uint8_t *out_count);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Set LED state on or off.
 * @ingroup LED
 * @param led LED handle.
 * @param index NeoPixel index to update. Ignored for non-NeoPixel LED types.
 * @param enabled `true` turns LED on, `false` turns LED off.
 * @retval true State update was applied.
 * @retval false Handle is invalid or LED type is unsupported.
 */
bool hw_led_set(hw_led_t *led, uint8_t index, bool enabled);

/**
 * @brief Set LED brightness.
 * @ingroup LED
 * @param led LED handle.
 * @param index NeoPixel index to update. Ignored for non-NeoPixel LED
 * types. For NeoPixel, brightness is per-index - each pixel keeps its own
 * color untouched and independently scaled.
 * @param percent Brightness percentage in [0.0, 100.0]. Values outside
 * this range are clamped. GPIO and Wi-Fi LED types have no intermediate
 * level - any nonzero value is just "on".
 * @retval true Brightness was applied.
 * @retval false Handle is invalid, or brightness control is unsupported
 * by this LED type.
 */
bool hw_led_set_brightness(hw_led_t *led, uint8_t index, float percent);

/**
 * @brief Set LED color.
 * @ingroup LED
 * @param led LED handle.
 * @param index NeoPixel index to update. Ignored for non-NeoPixel LED types.
 * @param color Color to apply, including alpha - see pix_color_t's own doc.
 * @retval true Color (or, for a fallback - see below - brightness) was
 * applied.
 * @retval false Handle is invalid.
 *
 * Only @ref hw_led_type_neopixel has a real color concept - every other
 * LED type falls back to @ref hw_led_set_brightness, deriving a
 * brightness percentage from @p color's perceived luma
 * (0.2*R + 0.7*G + 0.1*B, weighted for how much brighter green reads to
 * the eye than red or blue at the same channel value) scaled by alpha.
 * So `hw_led_set_color(led, 0, PIX_COLOR_RED)` on a plain GPIO/PWM/Wi-Fi
 * LED turns it on dim, not off, and a fully-transparent color (alpha 0)
 * always turns it off regardless of R/G/B, same as any other zero
 * brightness.
 */
bool hw_led_set_color(hw_led_t *led, uint8_t index, pix_color_t color);

/**
 * @brief Turn off all LED state, cancelling any active blink.
 * @ingroup LED
 * @param led LED handle.
 *
 * For NeoPixel LED types, every LED in the chain is turned off, not just a
 * single index.
 * @retval true State was cleared.
 * @retval false Handle is invalid or LED type is unsupported.
 */
bool hw_led_clear(hw_led_t *led);

/**
 * @brief Blink an LED using a timer.
 * @ingroup LED
 * @param led LED handle.
 * @param index NeoPixel index to update. Ignored for non-NeoPixel LED types.
 * @param period_ms How long each on/off phase lasts, in milliseconds - a
 * full on-then-off blink cycle takes twice this.
 * @param repeating When `true`, blink repeats (off, on, off, on, ...)
 * until @ref hw_led_set or @ref hw_led_clear is called to stop it. When
 * `false`, the LED turns on once, after one @p period_ms, and stays on.
 * @retval true Blink started.
 * @retval false Handle is invalid, or timer setup failed.
 *
 * The LED starts off (regardless of whatever state it was already in)
 * the moment this is called, and a timer takes over from there, flipping
 * it every @p period_ms.
 *
 * For hw_led_type_neopixel, the "on" phase's color is captured right
 * here, at call time - whatever @p index was last set to (@ref
 * hw_led_set_color, or plain white if @ref hw_led_set is all that was
 * ever used) - forced to full brightness (100% alpha) regardless of what
 * @ref hw_led_set_brightness may have left it at.
 * Change the color first, then call this, to blink a specific hue; every
 * other LED type has no color concept and just toggles fully on/off, the
 * same way @ref hw_led_set already does for them.
 *
 * Only one blink can be active per handle at a time - a hard limitation
 * for NeoPixel, whose whole chain shares this one handle, so two indices
 * can't blink independently. Calling this again while a blink is already
 * running - even for a different @p index - cancels it first, the same
 * as @ref hw_led_set or @ref hw_led_clear would, rather than failing.
 */
bool hw_led_blink(hw_led_t *led, uint8_t index, uint32_t period_ms,
                  bool repeating);

/** @} */
