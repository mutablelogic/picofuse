/**
 * @file ili9341.h
 * @brief ILI9341 TFT LCD controller interface.
 * @defgroup ILI9341 ILI9341
 * @ingroup Device
 *
 * This module provides a device-level API for ILI9341-driven TFT displays
 * over the 4-wire serial (SPI) interface: SCK/MOSI/MISO/CS carried by the
 * SPI bus itself, plus a separate `/DC` (data/command select) GPIO this
 * driver toggles around each command, and an optional `/RESET` GPIO for
 * boards that don't generate reset on their own.
 *
 * @note The device handle passed to dev_ili9341_init() must already be
 * open at the controller's required SPI mode - clock idles high, data
 * changes on the falling edge and is sampled on the rising edge (SPI mode
 * 3, hw_spi_mode_3) - via hw_spi_init() / hw_spi_init_default() /
 * hw_spi_init_device(). This driver only issues transfers on it; it does
 * not open or configure the bus itself.
 *
 * @note Some ILI9341-driven boards (including the Adafruit PiTFT this
 * driver was developed against) generate `/RESET` on-board from their own
 * power-on reset supervisor, tied to the same net as the touch
 * controller's reset - see dev/ft6236.h. On such boards, pass `NULL` for
 * `rst_pin`; dev_ili9341_init() falls back to the panel's software reset
 * command instead of driving a GPIO.
 */
#pragma once

#include <picofuse/hw.h>
#include <picofuse/pix.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

/**
 * @def DEV_ILI9341_WIDTH
 * @ingroup ILI9341
 * @brief Native panel width in pixels, at rotation 0.
 */
#define DEV_ILI9341_WIDTH 240u

/**
 * @def DEV_ILI9341_HEIGHT
 * @ingroup ILI9341
 * @brief Native panel height in pixels, at rotation 0.
 */
#define DEV_ILI9341_HEIGHT 320u

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque ILI9341 handle.
 * @ingroup ILI9341
 */
typedef struct dev_ili9341_t dev_ili9341_t;

/**
 * @brief Optional ILI9341 initialization options.
 * @ingroup ILI9341
 */
typedef struct {
  uint16_t rotation; ///< Initial rotation in degrees clockwise - one of 0
                     ///< (default), 90, 180, or 270. 90 and 270 swap
                     ///< dev_ili9341_size() relative to DEV_ILI9341_WIDTH /
                     ///< DEV_ILI9341_HEIGHT. Any other value is rejected by
                     ///< dev_ili9341_init().
} dev_ili9341_config_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Fill an ILI9341 config struct with safe defaults.
 * @ingroup ILI9341
 * @param config Config structure to initialize.
 */
void dev_ili9341_default_config(dev_ili9341_config_t *config);

/**
 * @brief Initialize an ILI9341-driven display over SPI.
 * @ingroup ILI9341
 * @param device SPI device handle from hw_spi_init() /
 * hw_spi_init_default() / hw_spi_init_device(), already open at
 * hw_spi_mode_3.
 * @param dc_pin GPIO handle for `/DC` (data/command select). Required -
 * the 4-wire serial protocol has no way to convey this otherwise.
 * @param rst_pin Optional GPIO handle for `/RESET`. Pass `NULL` on boards
 * that generate reset on their own (see the module-level note above); the
 * panel is then reset via its software-reset command instead.
 * @param config Optional pointer to initialization options. Pass `NULL`
 * to use default values.
 * @return ILI9341 handle, or `NULL` on failure - including an
 * @p config->rotation other than 0, 90, 180, or 270.
 */
dev_ili9341_t *dev_ili9341_init(hw_deviceio_t *device, hw_gpio_t *dc_pin,
                                hw_gpio_t *rst_pin,
                                const dev_ili9341_config_t *config);

/**
 * @brief Deinitialize an ILI9341 handle.
 * @ingroup ILI9341
 * @param ili9341 ILI9341 handle.
 *
 * Passing `NULL` is safe and is a no-op.
 */
void dev_ili9341_deinit(dev_ili9341_t *ili9341);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Get the display size at the configured rotation.
 * @ingroup ILI9341
 * @param ili9341 ILI9341 handle.
 * @param out_size Output destination for the display size. Left
 * unmodified if @p ili9341 is invalid.
 */
void dev_ili9341_size(const dev_ili9341_t *ili9341, pix_size_t *out_size);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Write a rectangular block of pixels.
 * @ingroup ILI9341
 * @param ili9341 ILI9341 handle.
 * @param origin Top-left corner to write to, within the current rotation's
 * bounds.
 * @param bitmap Source bitmap. Must be @ref PIX_FMT_RGB565 - native byte
 * order; this function handles the wire's big-endian byte order
 * internally. `bitmap->stride` is honored, so the source need not be
 * tightly packed.
 * @retval true Write succeeded.
 * @retval false The handle is invalid, `bitmap` is `NULL` or not
 * PIX_FMT_RGB565, or the rectangle falls outside the current rotation's
 * bounds.
 */
bool dev_ili9341_write(dev_ili9341_t *ili9341, pix_point_t origin,
                       const pix_bitmap_t *bitmap);

/** @} */
