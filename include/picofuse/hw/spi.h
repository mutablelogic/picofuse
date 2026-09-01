/**
 * @file spi.h
 * @brief SPI (Serial Peripheral Interface) interface
 * @defgroup SPI SPI
 * @ingroup DeviceIO
 *
 * Serial Peripheral Interface (SPI) interface for hardware platforms. This
 * module provides functions to initialize SPI peripherals in master mode
 * and configure device-specific framing; transfers are performed through
 * the generic hw_deviceio_t interface (see deviceio.h) that hw_spi_init*()
 * returns a handle to.
 */
#pragma once
#include "deviceio.h"
#include "gpio.h"
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief SPI mode selection.
 * @ingroup SPI
 */
typedef enum {
  hw_spi_mode_0 =
      0, ///< CPOL = 0, CPHA = 0 (clock idles low; sample on rising edge)
  hw_spi_mode_1 =
      1, ///< CPOL = 0, CPHA = 1 (clock idles low; sample on falling edge)
  hw_spi_mode_2 =
      2, ///< CPOL = 1, CPHA = 0 (clock idles high; sample on falling edge)
  hw_spi_mode_3 =
      3, ///< CPOL = 1, CPHA = 1 (clock idles high; sample on rising edge)
} hw_spi_mode_t;

/**
 * @brief SPI initialization configuration.
 * @ingroup SPI
 *
 * Describes optional SPI settings beyond the required baud rate passed
 * directly to the init functions.
 *
 * When `NULL` is passed to an init function, implementation defaults are
 * used for all fields in this structure. The default SPI configuration is
 * chip-select active low, mode 0, and 8 bits per word.
 */
typedef struct {
  bool cs_active_low;    ///< True when chip-select is active low.
  hw_spi_mode_t mode;    ///< SPI mode selection.
  uint8_t bits_per_word; ///< SPI frame size in bits.
} hw_spi_config_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize an SPI interface using the platform default adapter and
 * pins.
 * @ingroup SPI
 * @param baud_rate Desired SPI clock rate in Hz.
 * @param config Optional pointer to extended SPI configuration. Pass `NULL`
 * to use default mode and frame size.
 * @return Device I/O handle bound to the SPI device, or `NULL` on failure.
 * Release it with hw_deviceio_deinit() - there is no separate
 * hw_spi_deinit().
 * @note On platforms where SPI buses are selected by device path instead of
 * index (for example Linux), this function may be unsupported and return
 * `NULL` by design. Use `hw_spi_init_device()` on those platforms.
 */
hw_deviceio_t *hw_spi_init_default(uint32_t baud_rate,
                                   const hw_spi_config_t *config);

/**
 * @brief Initialize an SPI interface with a specific adapter and pins.
 * @ingroup SPI
 * @param index SPI adapter index to use.
 * @param sck_pin GPIO handle for SCK.
 * @param tx_pin GPIO handle for MOSI.
 * @param rx_pin GPIO handle for MISO.
 * @param cs_pin Optional GPIO handle for CS. Pass NULL to leave CS unmanaged.
 * @param baud_rate Desired SPI clock rate in Hz.
 * @param config Optional pointer to extended SPI configuration. Pass `NULL`
 * to use default mode and frame size.
 * @return Device I/O handle bound to the SPI device, or `NULL` on failure.
 * Release it with hw_deviceio_deinit() - there is no separate
 * hw_spi_deinit().
 */
hw_deviceio_t *hw_spi_init(uint8_t index, hw_gpio_t *sck_pin,
                           hw_gpio_t *tx_pin, hw_gpio_t *rx_pin,
                           hw_gpio_t *cs_pin, uint32_t baud_rate,
                           const hw_spi_config_t *config);

/**
 * @brief Initialize an SPI interface from a platform-specific device path.
 * @ingroup SPI
 * @param device Device identifier such as `/dev/spidev0.0`.
 * @param baud_rate Desired SPI clock rate in Hz.
 * @param config Optional pointer to extended SPI configuration. Pass `NULL`
 * to use default mode and frame size.
 * @return Device I/O handle bound to the SPI device, or `NULL` on failure.
 * Release it with hw_deviceio_deinit() - there is no separate
 * hw_spi_deinit().
 *
 * This entry point is intended for platforms where SPI buses are exposed as
 * named devices rather than a fixed, enumerable set of adapters.
 */
hw_deviceio_t *hw_spi_init_device(const char *device, uint32_t baud_rate,
                                  const hw_spi_config_t *config);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Get the total number of available SPI adapters.
 * @ingroup SPI
 * @return Number of SPI adapters available on the current platform.
 *
 * On platforms that open SPI buses by device path, this may return `0` even
 * when SPI is supported. In that case, use `hw_spi_init_device()` instead of
 * enumerating adapters by index.
 */
uint8_t hw_spi_count(void);

/** @} */
