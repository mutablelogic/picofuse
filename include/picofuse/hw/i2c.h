/**
 * @file i2c.h
 * @brief I2C (Inter-Integrated Circuit) interface
 * @defgroup I2C I2C
 * @ingroup DeviceIO
 *
 * Inter-Integrated Circuit (I2C) interface for hardware platforms. This
 * module provides functions to initialize I2C peripherals in master mode,
 * bound to a specific device address; transfers are performed through the
 * generic hw_deviceio_t interface (see deviceio.h) that hw_i2c_init*()
 * returns a handle to. Every bus runs at the fixed HW_I2C_BAUD_RATE - I2C
 * has no per-transfer rate negotiation, so there's nothing to gain from
 * letting different devices on the same bus disagree about it.
 */
#pragma once
#include "deviceio.h"
#include "gpio.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @def HW_I2C_BAUD_RATE
 * @ingroup I2C
 * @brief Fixed I2C baud rate in Hz, used for every bus. Standard-mode I2C
 * (100kHz) is supported by the widest range of devices; override before
 * including this header if a specific application needs a different rate.
 */
#ifndef HW_I2C_BAUD_RATE
#define HW_I2C_BAUD_RATE 100000
#endif

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize an I2C interface using the platform default adapter and
 * pins.
 * @ingroup I2C
 * @param addr 7-bit address of the device to communicate with.
 * @return Device I/O handle bound to the device at `addr`, or `NULL` on
 * failure. Release it with hw_deviceio_deinit() - there is no separate
 * hw_i2c_deinit().
 * @note On platforms where I2C buses are selected by device path instead of
 * index (for example Linux), this function may be unsupported and return
 * `NULL` by design. Use `hw_i2c_init_device()` on those platforms.
 */
hw_deviceio_t *hw_i2c_init_default(uint8_t addr);

/**
 * @brief Initialize an I2C interface with a specific adapter and pins.
 * @ingroup I2C
 * @param index I2C adapter index to use.
 * @param addr 7-bit address of the device to communicate with.
 * @param sda_pin GPIO handle for SDA.
 * @param scl_pin GPIO handle for SCL.
 * @return Device I/O handle bound to the device at `addr`, or `NULL` on
 * failure. Release it with hw_deviceio_deinit() - there is no separate
 * hw_i2c_deinit().
 */
hw_deviceio_t *hw_i2c_init(uint8_t index, uint8_t addr, hw_gpio_t *sda_pin,
                           hw_gpio_t *scl_pin);

/**
 * @brief Initialize an I2C interface from a platform-specific device path.
 * @ingroup I2C
 * @param device Device identifier such as `/dev/i2c-1`.
 * @param addr 7-bit address of the device to communicate with.
 * @return Device I/O handle bound to the device at `addr`, or `NULL` on
 * failure. Release it with hw_deviceio_deinit() - there is no separate
 * hw_i2c_deinit().
 *
 * This entry point is intended for platforms where I2C buses are exposed as
 * named devices rather than a fixed, enumerable set of adapters.
 */
hw_deviceio_t *hw_i2c_init_device(const char *device, uint8_t addr);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Get the total number of available I2C adapters.
 * @ingroup I2C
 * @return Number of I2C adapters available on the current platform.
 *
 * On platforms that open I2C buses by device path, this may return `0` even
 * when I2C is supported. In that case, use `hw_i2c_init_device()` instead of
 * enumerating adapters by index.
 */
uint8_t hw_i2c_count(void);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Probe whether any device acknowledges an address on a bus.
 * @ingroup I2C
 * @param device Any already-open handle from hw_i2c_init*() - used only to
 * reach its bus; `addr` is probed independently of whichever address
 * `device` itself was opened with. Has no SPI equivalent (SPI has no
 * shared, addressable bus to scan), which is why this lives here rather
 * than in deviceio.h.
 * @param addr 7-bit address to probe.
 * @retval true Something acknowledged `addr`.
 * @retval false Nothing acknowledged `addr`, `addr` is a reserved address,
 * or `device` isn't a valid, open I2C handle.
 *
 * Some devices only ACK a read, others only a write, during scan-style
 * probing with no register address involved - both are tried.
 */
bool hw_i2c_detect(hw_deviceio_t *device, uint8_t addr);

/** @} */
