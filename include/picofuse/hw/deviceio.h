/**
 * @file deviceio.h
 * @brief Abstract device I/O interface
 * @defgroup DeviceIO Device I/O
 * @ingroup Hardware
 *
 * A bus-agnostic interface for transferring data to and from addressable
 * devices - register reads/writes and raw transfers - without the caller
 * needing to know whether the underlying bus is I2C, SPI, or (in future)
 * something like an FTDI adapter. hw_deviceio_t is the one handle type
 * every backend hands back - hw_spi_init*() (see spi.h) and hw_i2c_init*()
 * construct and fully own one directly, rather than wrapping some separate
 * hw_spi_t/hw_i2c_t - so a device driver written against hw_deviceio_t
 * works unchanged regardless of which bus actually carries it.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @def HW_DEVICEIO_CAPACITY
 * @ingroup DeviceIO
 * @brief Maximum number of open device I/O handles, across every backend
 * (SPI, I2C, ...) combined.
 */
#ifndef HW_DEVICEIO_CAPACITY
#define HW_DEVICEIO_CAPACITY 8
#endif

/**
 * @def HW_DEVICEIO_CONTEXT_SIZE
 * @ingroup DeviceIO
 * @brief Size in bytes of the per-handle scratch context space embedded in
 * every hw_deviceio_t, for a backend's own private per-device state.
 */
#ifndef HW_DEVICEIO_CONTEXT_SIZE
#define HW_DEVICEIO_CONTEXT_SIZE 32
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque device I/O handle.
 * @ingroup DeviceIO
 * @headerfile deviceio.h hw/hw.h
 */
typedef struct hw_deviceio_t hw_deviceio_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Release a device I/O handle.
 * @ingroup DeviceIO
 * @param device Device handle.
 *
 * Fully releases whatever backend resources `device` holds (e.g. an SPI
 * adapter's claimed pins, an I2C bus's peripheral) as well as the handle
 * itself - there is no separate bus-specific deinit to call afterward.
 * Safe to call on NULL.
 */
void hw_deviceio_deinit(hw_deviceio_t *device);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Perform a raw, bidirectional transfer.
 * @ingroup DeviceIO
 * @param device Device handle.
 * @param data Buffer used for transmitted and received bytes.
 * @param tx Number of bytes to transmit from `data`.
 * @param rx Number of bytes to receive into `data + tx`.
 * @param timeout_ms Timeout in milliseconds for the operation. Set to `0`
 * to use the backend's default transfer path.
 * @return Number of bytes transferred, or `0` on failure.
 *
 * Supports write-only (`tx > 0, rx == 0`), read-only (`tx == 0, rx > 0`),
 * and write-then-read (`tx > 0, rx > 0`) transfers. The exact semantics of
 * a write-then-read - a repeated start on I2C, a continuous chip-select
 * assertion on SPI - are defined by whichever bus `device` is actually
 * bound to.
 */
size_t hw_deviceio_xfr(hw_deviceio_t *device, void *data, size_t tx, size_t rx,
                       uint32_t timeout_ms);

/**
 * @brief Read bytes from a register on a device.
 * @ingroup DeviceIO
 * @param device Device handle.
 * @param reg Register address to read from.
 * @param data Buffer to receive the bytes.
 * @param len Number of bytes to read.
 * @param timeout_ms Timeout in milliseconds for the operation. Set to `0`
 * to use the backend's default transfer path.
 * @return Number of bytes read, or `0` on failure.
 */
size_t hw_deviceio_read_reg(hw_deviceio_t *device, uint8_t reg, void *data,
                            size_t len, uint32_t timeout_ms);

/**
 * @brief Write bytes to a register on a device.
 * @ingroup DeviceIO
 * @param device Device handle.
 * @param reg Register address to write to.
 * @param data Buffer containing bytes to write.
 * @param len Number of bytes to write.
 * @param timeout_ms Timeout in milliseconds for the operation. Set to `0`
 * to use the backend's default transfer path.
 * @return Number of bytes written, or `0` on failure.
 */
size_t hw_deviceio_write_reg(hw_deviceio_t *device, uint8_t reg,
                             const void *data, size_t len, uint32_t timeout_ms);

/** @} */
