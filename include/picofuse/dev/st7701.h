/**
 * @file st7701.h
 * @brief ST7701 TFT LCD controller interface.
 * @defgroup ST7701 ST7701
 * @ingroup Display
 */
#pragma once

#include <picofuse/hw.h>
#include <picofuse/pix.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Optional ST7701 initialization options.
 * @ingroup ST7701
 */
typedef struct {
  uint16_t rotation;
} dev_st7701_config_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Fill an ST7701 config struct with safe defaults.
 * @ingroup ST7701
 * @param config Config structure to initialize.
 */
void dev_st7701_default_config(dev_st7701_config_t *config);

/**
 * @brief Initialize an ST7701-driven display over SPI.
 * @ingroup ST7701
 * @param device SPI device handle. This panel has no D/CX GPIO - `device`
 * must be a hw_spi_init() handle configured with `bits_per_word = 9`,
 * command/data select packed as the 9th bit of every word (see
 * hw_deviceio_xfr()).
 * @param size Panel resolution.
 * @param bl_pin Optional GPIO handle for the backlight. Pass `NULL` if
 * the backlight isn't software-controlled.
 * @param config Optional pointer to initialization options. Pass `NULL`
 * to use default values.
 * @return display handle, or `NULL` on failure
 */
pix_display_t *dev_st7701_init(hw_deviceio_t *device, pix_size_t size,
                               hw_gpio_t *bl_pin,
                               const dev_st7701_config_t *config);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Set the backlight brightness.
 * @ingroup ST7701
 * @param display Display handle from dev_st7701_init().
 * @param brightness Brightness from `0` (off) to `255` (full).
 * @return `false` if `display` wasn't created by dev_st7701_init(), or has
 * no backlight pin.
 */
bool dev_st7701_set_backlight(pix_display_t *display, uint8_t brightness);

/** @} */
