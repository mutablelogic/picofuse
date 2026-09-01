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
 * returns a handle to.
 */
#pragma once
#include "deviceio.h"
#include "gpio.h"
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize an I2C interface using the platform default adapter and
 * pins.
 * @ingroup I2C
 * @param addr 7-bit address of the device to communicate with.
 * @param baud_rate Desired I2C baud rate in Hz. `baud_rate` configures the
 * whole bus, not just this device - if another device on the same bus is
 * already active at a different rate, this call fails.
 * @return Device I/O handle bound to the device at `addr`, or `NULL` on
 * failure (including a `baud_rate` that conflicts with another device
 * already active on this bus). Release it with hw_deviceio_deinit() -
 * there is no separate hw_i2c_deinit().
 * @note On platforms where I2C buses are selected by device path instead of
 * index (for example Linux), this function may be unsupported and return
 * `NULL` by design. Use `hw_i2c_init_device()` on those platforms.
 */
hw_deviceio_t *hw_i2c_init_default(uint8_t addr, uint32_t baud_rate);

/**
 * @brief Initialize an I2C interface with a specific adapter and pins.
 * @ingroup I2C
 * @param index I2C adapter index to use.
 * @param addr 7-bit address of the device to communicate with.
 * @param sda_pin GPIO handle for SDA.
 * @param scl_pin GPIO handle for SCL.
 * @param baud_rate Desired I2C baud rate in Hz. `baud_rate` configures the
 * whole bus, not just this device - if another device on the same `index`
 * is already active at a different rate, this call fails.
 * @return Device I/O handle bound to the device at `addr`, or `NULL` on
 * failure (including a `baud_rate` that conflicts with another device
 * already active on `index`). Release it with hw_deviceio_deinit() - there
 * is no separate hw_i2c_deinit().
 */
hw_deviceio_t *hw_i2c_init(uint8_t index, uint8_t addr, hw_gpio_t *sda_pin,
                           hw_gpio_t *scl_pin, uint32_t baud_rate);

/**
 * @brief Initialize an I2C interface from a platform-specific device path.
 * @ingroup I2C
 * @param device Device identifier such as `/dev/i2c-1`.
 * @param addr 7-bit address of the device to communicate with.
 * @param baud_rate Desired I2C baud rate in Hz. `baud_rate` configures the
 * whole bus, not just this device - if another device on the same
 * `device` path is already active at a different rate, this call fails.
 * @return Device I/O handle bound to the device at `addr`, or `NULL` on
 * failure (including a `baud_rate` that conflicts with another device
 * already active on this bus). Release it with hw_deviceio_deinit() -
 * there is no separate hw_i2c_deinit().
 *
 * This entry point is intended for platforms where I2C buses are exposed as
 * named devices rather than a fixed, enumerable set of adapters.
 */
hw_deviceio_t *hw_i2c_init_device(const char *device, uint8_t addr,
                                  uint32_t baud_rate);

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
