#include "private.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pico is the only platform in this build where the pool below can
// genuinely be raced from two physical cores at once - see the note on
// _hw_deviceio_alloc_handle() and hw_deviceio_deinit(). Host platforms
// keep the plain unlocked scan already used by, e.g., _sys_iostream_alloc().
#ifdef SYSTEM_NAME_PICO
#include "../../sys/pico/sync.h"
#define _HW_DEVICEIO_LOCK() _sys_sync_pool_lock()
#define _HW_DEVICEIO_UNLOCK() _sys_sync_pool_unlock()
#else
#define _HW_DEVICEIO_LOCK()
#define _HW_DEVICEIO_UNLOCK()
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Device I/O handle.
 *
 * Deliberately private to this file - a backend never sees this layout,
 * only the `ops` it handed to `_hw_deviceio_alloc_handle()` and the
 * embedded `context` scratch buffer it gets back via
 * `_hw_deviceio_context()`. Embedding the context here, rather than each
 * backend keeping its own separate pool of per-device structs alongside
 * this one, means there's only ever one pool (this one) to get cross-core
 * allocation safety right for - see _hw_deviceio_alloc().
 */
struct hw_deviceio_t {
  const hw_deviceio_ops_t *ops;
  hw_deviceio_bus_t bus;
  _Alignas(max_align_t) uint8_t context[HW_DEVICEIO_CONTEXT_SIZE];
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_deviceio_t _hw_deviceio_pool[HW_DEVICEIO_CAPACITY] = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief A handle only counts as usable once init has actually bound it to
 * a backend, by whichever hw_*_init() built it. */
static inline bool _hw_deviceio_valid(const hw_deviceio_t *device) {
  return device != NULL && device->ops != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS (see private.h)

/** @brief Claims a free slot from the static instance pool, or NULL if
 * every slot is already in use. A slot is free when its `ops` field is
 * NULL - hw_deviceio_deinit() clears it back to NULL on release.
 *
 * Must be called with _HW_DEVICEIO_LOCK() held, and the caller must set
 * `ops` before releasing it - marking the slot used is what closes the
 * window a concurrent caller on the other core could otherwise see the
 * same slot as still free (see hw/pico/dma.c's _hw_dma_fifo_alloc(), which
 * had exactly this bug: the slot wasn't marked used until several lines
 * after the scan that found it, long enough for the other core to pick
 * the same one). */
static inline hw_deviceio_t *_hw_deviceio_alloc(void) {
  for (size_t i = 0; i < HW_DEVICEIO_CAPACITY; i++) {
    if (_hw_deviceio_pool[i].ops == NULL) {
      return &_hw_deviceio_pool[i];
    }
  }
  return NULL;
}

hw_deviceio_t *_hw_deviceio_alloc_handle(const hw_deviceio_ops_t *ops,
                                         hw_deviceio_bus_t bus) {
  if (ops == NULL) {
    return NULL;
  }

  _HW_DEVICEIO_LOCK();
  hw_deviceio_t *device = _hw_deviceio_alloc();
  if (device != NULL) {
    memset(device->context, 0, sizeof(device->context));
    device->bus = bus;
    device->ops = ops;
  }
  _HW_DEVICEIO_UNLOCK();
  return device;
}

void *_hw_deviceio_context(const hw_deviceio_t *device) {
  return _hw_deviceio_valid(device) ? (void *)device->context : NULL;
}

const hw_deviceio_ops_t *_hw_deviceio_ops(const hw_deviceio_t *device) {
  return device != NULL ? device->ops : NULL;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

hw_deviceio_bus_t hw_deviceio_bus(const hw_deviceio_t *device) {
  return _hw_deviceio_valid(device) ? device->bus : hw_deviceio_i2c;
}

void hw_deviceio_deinit(hw_deviceio_t *device) {
  if (!_hw_deviceio_valid(device)) {
    return;
  }
  if (device->ops->deinit != NULL) {
    device->ops->deinit(device);
  }
  // Releases the pool slot back to _hw_deviceio_alloc() - locked against
  // the other core concurrently scanning/claiming a slot (see
  // _hw_deviceio_alloc_handle()). ops is what marks the slot free; the
  // context buffer is left as-is, since _hw_deviceio_alloc_handle() zeroes
  // it fresh for whichever backend claims the slot next.
  _HW_DEVICEIO_LOCK();
  device->ops = NULL;
  _HW_DEVICEIO_UNLOCK();
}

size_t hw_deviceio_xfr(hw_deviceio_t *device, void *data, size_t tx, size_t rx,
                       uint32_t timeout_ms) {
  if (!_hw_deviceio_valid(device) || device->ops->xfr == NULL) {
    return 0;
  }
  return device->ops->xfr(device, data, tx, rx, timeout_ms);
}

size_t hw_deviceio_read_reg(hw_deviceio_t *device, uint8_t reg, void *data,
                            size_t len, uint32_t timeout_ms) {
  if (!_hw_deviceio_valid(device) || device->ops->read_reg == NULL) {
    return 0;
  }
  return device->ops->read_reg(device, reg, data, len, timeout_ms);
}

size_t hw_deviceio_write_reg(hw_deviceio_t *device, uint8_t reg,
                             const void *data, size_t len,
                             uint32_t timeout_ms) {
  if (!_hw_deviceio_valid(device) || device->ops->write_reg == NULL) {
    return 0;
  }
  return device->ops->write_reg(device, reg, data, len, timeout_ms);
}
