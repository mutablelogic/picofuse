#pragma once
#include <picofuse/hw/deviceio.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Backend operations for a device I/O handle.
 *
 * hw_deviceio_t isn't backed by any one bus - it could be an SPI device, an
 * I2C device, or (in future) something sitting behind an FTDI adapter - so
 * each backend (see src/picofuse/hw/pico/spi.c, i2c.c, ...) implements this
 * and hands it to `_hw_deviceio_alloc_handle()` at its own `hw_*_init()` time.
 * Every public `hw_deviceio_*` operation (see deviceio.c) dispatches
 * through whichever table the handle was constructed with.
 *
 * `deinit` is optional (NULL is a valid no-op) - the others are only ever
 * called if non-NULL too, so a backend that has no meaningful `read_reg`
 * (say) can simply leave it unset rather than provide a stub.
 */
typedef struct hw_deviceio_ops_t {
  size_t (*xfr)(hw_deviceio_t *device, void *data, size_t tx, size_t rx,
               uint32_t timeout_ms);
  size_t (*read_reg)(hw_deviceio_t *device, uint8_t reg, void *data,
                     size_t len, uint32_t timeout_ms);
  size_t (*write_reg)(hw_deviceio_t *device, uint8_t reg, const void *data,
                      size_t len, uint32_t timeout_ms);
  void (*deinit)(hw_deviceio_t *device);
} hw_deviceio_ops_t;

/**
 * @brief Allocate a handle bound to a backend.
 *
 * `hw_deviceio_t`'s own layout stays private to deviceio.c - this is the
 * only way a backend gets to create one. The returned handle carries a
 * zeroed HW_DEVICEIO_CONTEXT_SIZE-byte scratch buffer for the backend's own
 * per-device state (see _hw_deviceio_context()) - populate it after this
 * returns, there's no way to seed it here. Returns `NULL` if `ops` is
 * `NULL` or the static pool (HW_DEVICEIO_CAPACITY) is full.
 */
hw_deviceio_t *_hw_deviceio_alloc_handle(const hw_deviceio_ops_t *ops);

/**
 * @brief Get a handle's embedded scratch context buffer.
 * @return Pointer to `device`'s HW_DEVICEIO_CONTEXT_SIZE-byte buffer, valid
 * for as long as `device` is (i.e. until hw_deviceio_deinit()), or `NULL`
 * if `device` is invalid. Cast to whatever struct the owning backend
 * stores there - guard the fit with a `_Static_assert(sizeof(your_struct)
 * <= HW_DEVICEIO_CONTEXT_SIZE, ...)`.
 */
void *_hw_deviceio_context(const hw_deviceio_t *device);

/**
 * @brief Get the ops table a handle was constructed with.
 * @return The same pointer passed to `_hw_deviceio_alloc_handle()`, or
 * `NULL` if `device` is invalid.
 *
 * A backend can compare this against its own static `hw_deviceio_ops_t`
 * instance to check that a handle passed to one of its own extra,
 * backend-specific functions (e.g. I2C's hw_i2c_detect(), which has no
 * SPI equivalent) actually is one of its own, rather than blindly
 * reinterpreting another backend's embedded context.
 */
const hw_deviceio_ops_t *_hw_deviceio_ops(const hw_deviceio_t *device);
