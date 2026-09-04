/**
 * @file led.h
 * @brief Board LED helpers.
 * @defgroup LED LED
 * @ingroup Hardware
 */
#pragma once
#include "gpio.h"
#include "pwm.h"
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

/**
 * @brief Sentinel returned when no default board LED GPIO is available.
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
 * @param period_ms Blink period in milliseconds.
 * @param repeating When `true`, blink repeats until @ref hw_led_set or
 * @ref hw_led_clear is called to stop it. When `false`, LED is turned on
 * immediately and turned off once after one period.
 * @retval true Blink started.
 * @retval false Handle is invalid, blink is already active on this handle, or
 * timer setup failed.
 *
 * @todo Not implemented yet - always returns false. Planned for a separate
 * PR once the rest of the LED module has landed.
 */
bool hw_led_blink(hw_led_t *led, uint8_t index, uint32_t period_ms,
                  bool repeating);

/** @} */
