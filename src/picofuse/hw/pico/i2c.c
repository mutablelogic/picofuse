#include "../../sys/pico/sync.h"
#include "../deviceio/private.h"
#include <assert.h>
#include <hardware/i2c.h>
#include <pico/mutex.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Per-adapter (i2c0/i2c1) shared state
typedef struct hw_i2c_bus_t {
  i2c_inst_t *instance;
  hw_gpio_t *sda_pin;
  hw_gpio_t *scl_pin;
  mutex_t lock;
  size_t refcount;
  bool owns_pins;
} hw_i2c_bus_t;

// Per-device context
typedef struct hw_i2c_ctx_t {
  hw_i2c_bus_t *bus;
  uint8_t addr;
} hw_i2c_ctx_t;

static_assert(sizeof(hw_i2c_ctx_t) <= HW_DEVICEIO_CONTEXT_SIZE,
              "hw_i2c_ctx_t too large for hw_deviceio_t's embedded context");

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_i2c_bus_t _hw_i2c_bus[NUM_I2CS] = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief Valid 7-bit addresses exclude the reserved 0000xxx/1111xxx
 * blocks, per the I2C spec. */
static inline bool _hw_i2c_valid_addr(uint8_t addr) {
  if (addr < 0x08 || addr > 0x77) {
    return false;
  }
  return (addr & 0x78u) != 0x00u && (addr & 0x78u) != 0x78u;
}

/** @brief RP2040/RP2350: each GPIO's low 2 bits fix which I2C instance and
 * signal (SDA/SCL) it can serve - pin 0=I2C0 SDA, 1=I2C0 SCL, 2=I2C1 SDA,
 * 3=I2C1 SCL, repeating every 4 pins. */
static inline bool _hw_i2c_pin_matches(const hw_gpio_t *pin, uint8_t index,
                                       uint8_t signal_offset) {
  if (pin == NULL) {
    return false;
  }
  uint8_t pin_num = hw_gpio_pin(pin);
  return (pin_num & 0x3u) == (uint8_t)(index * 2u + signal_offset);
}

/** @brief Wrapper around i2c_write_blocking() and i2c_write_timeout_us()  */
static inline int _hw_i2c_write(i2c_inst_t *instance, uint8_t addr,
                                const uint8_t *data, size_t len, bool nostop,
                                uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    return i2c_write_blocking(instance, addr, data, len, nostop);
  }
  return i2c_write_timeout_us(instance, addr, data, len, nostop,
                              timeout_ms * 1000u);
}

/** @brief Wrapper around i2c_read_blocking() and i2c_read_timeout_us() */
static inline int _hw_i2c_read(i2c_inst_t *instance, uint8_t addr,
                               uint8_t *data, size_t len, bool nostop,
                               uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    return i2c_read_blocking(instance, addr, data, len, nostop);
  }
  return i2c_read_timeout_us(instance, addr, data, len, nostop,
                             timeout_ms * 1000u);
}

/** @brief True if `device` was constructed by one of hw_i2c_init*() -
 * needed by I2C-specific functions that take an hw_deviceio_t* directly
 * (hw_i2c_detect()) rather than going through hw_deviceio_*()'s own
 * dispatch, since those don't otherwise check which backend actually
 * owns the handle before reinterpreting its embedded context. */
static inline bool _hw_i2c_valid(const hw_deviceio_t *device) {
  return device != NULL && _hw_deviceio_ops(device) == &_hw_i2c_ops;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - OPS

static size_t _hw_i2c_ops_xfr(hw_deviceio_t *device, void *data, size_t tx,
                              size_t rx, uint32_t timeout_ms) {
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  if ((tx > 0 || rx > 0) && data == NULL) {
    return 0;
  }

  uint8_t *bytes = data;
  size_t transferred = 0;

  mutex_enter_blocking(&ctx->bus->lock);

  if (tx > 0) {
    int ret = _hw_i2c_write(ctx->bus->instance, ctx->addr, bytes, tx, rx > 0,
                            timeout_ms);
    if (ret != (int)tx) {
      mutex_exit(&ctx->bus->lock);
      return 0;
    }
    transferred += tx;
  }

  if (rx > 0) {
    int ret = _hw_i2c_read(ctx->bus->instance, ctx->addr, bytes + tx, rx, false,
                           timeout_ms);
    if (ret != (int)rx) {
      mutex_exit(&ctx->bus->lock);
      return 0;
    }
    transferred += rx;
  }

  mutex_exit(&ctx->bus->lock);
  return transferred;
}

static size_t _hw_i2c_ops_read_reg(hw_deviceio_t *device, uint8_t reg,
                                   void *data, size_t len,
                                   uint32_t timeout_ms) {
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  if (data == NULL || len == 0) {
    return 0;
  }

  size_t result = 0;
  mutex_enter_blocking(&ctx->bus->lock);

  if (_hw_i2c_write(ctx->bus->instance, ctx->addr, &reg, sizeof(reg), true,
                    timeout_ms) == (int)sizeof(reg) &&
      _hw_i2c_read(ctx->bus->instance, ctx->addr, data, len, false,
                   timeout_ms) == (int)len) {
    result = len;
  }

  mutex_exit(&ctx->bus->lock);
  return result;
}

static size_t _hw_i2c_ops_write_reg(hw_deviceio_t *device, uint8_t reg,
                                    const void *data, size_t len,
                                    uint32_t timeout_ms) {
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  if (len > 0 && data == NULL) {
    return 0;
  }

  // Prefix the register address onto the write in one shot, using a stack
  // buffer for the common small-write case to avoid heap overhead.
  size_t total_len = len + 1;
  uint8_t stack_buffer[32];
  uint8_t *buffer = stack_buffer;
  bool use_heap = total_len > sizeof(stack_buffer);
  if (use_heap) {
    buffer = sys_malloc(total_len);
    if (buffer == NULL) {
      return 0;
    }
  }

  buffer[0] = reg;
  if (len > 0) {
    memcpy(buffer + 1, data, len);
  }

  mutex_enter_blocking(&ctx->bus->lock);
  int ret = _hw_i2c_write(ctx->bus->instance, ctx->addr, buffer, total_len,
                          false, timeout_ms);
  mutex_exit(&ctx->bus->lock);

  if (use_heap) {
    sys_free(buffer);
  }

  return ret == (int)total_len ? len : 0;
}

static void _hw_i2c_ops_deinit(hw_deviceio_t *device) {
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  hw_i2c_bus_t *bus = ctx->bus;

  _sys_sync_pool_lock();

  if (--bus->refcount > 0) {
    _sys_sync_pool_unlock();
    return;
  }

  i2c_deinit(bus->instance);
  bool owns_pins = bus->owns_pins;
  hw_gpio_t *sda_pin = bus->sda_pin;
  hw_gpio_t *scl_pin = bus->scl_pin;
  memset(bus, 0, sizeof(*bus));
  _sys_sync_pool_unlock();

  if (owns_pins) {
    hw_gpio_deinit(sda_pin);
    hw_gpio_deinit(scl_pin);
  }
}

static const hw_deviceio_ops_t _hw_i2c_ops = {
    .xfr = _hw_i2c_ops_xfr,
    .read_reg = _hw_i2c_ops_read_reg,
    .write_reg = _hw_i2c_ops_write_reg,
    .deinit = _hw_i2c_ops_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

uint8_t hw_i2c_count(void) {
  return NUM_I2CS > UINT8_MAX ? UINT8_MAX : (uint8_t)NUM_I2CS;
}

hw_deviceio_t *hw_i2c_init_default(uint8_t addr) {
#if defined(PICO_DEFAULT_I2C) && defined(PICO_DEFAULT_I2C_SDA_PIN) &&          \
    defined(PICO_DEFAULT_I2C_SCL_PIN)
  sys_debugf("hw", "i2c_init_default: index=%u sda=%u scl=%u addr=%u",
             PICO_DEFAULT_I2C, PICO_DEFAULT_I2C_SDA_PIN,
             PICO_DEFAULT_I2C_SCL_PIN, addr);

  // Already running (another device on this bus already opened it) - join
  // it directly, without touching pins again.
  if (_hw_i2c_bus[PICO_DEFAULT_I2C].refcount > 0) {
    return hw_i2c_init(PICO_DEFAULT_I2C, addr, NULL, NULL);
  }

  hw_gpio_t *sda_pin = hw_gpio_init(0, PICO_DEFAULT_I2C_SDA_PIN, hw_gpio_i2c);
  if (sda_pin == NULL) {
    return NULL;
  }
  hw_gpio_t *scl_pin = hw_gpio_init(0, PICO_DEFAULT_I2C_SCL_PIN, hw_gpio_i2c);
  if (scl_pin == NULL) {
    hw_gpio_deinit(sda_pin);
    return NULL;
  }

  hw_deviceio_t *device = hw_i2c_init(PICO_DEFAULT_I2C, addr, sda_pin, scl_pin);
  if (device == NULL) {
    hw_gpio_deinit(sda_pin);
    hw_gpio_deinit(scl_pin);
    return NULL;
  }

  // The refcount==0 check above and this call aren't atomic together, so
  // the other core could have won a race to bring the same bus up first,
  // in between - if so, hw_i2c_init() just joined its bus, leaving these
  // pins redundant rather than the ones actually in use.
  hw_i2c_bus_t *bus = &_hw_i2c_bus[PICO_DEFAULT_I2C];
  if (bus->sda_pin == sda_pin) {
    bus->owns_pins = true;
  } else {
    hw_gpio_deinit(sda_pin);
    hw_gpio_deinit(scl_pin);
  }
  return device;
#else
  sys_debugf("hw", "i2c_init_default: unsupported on this target (addr=%u)",
             addr);
  (void)addr;
  return NULL;
#endif
}

hw_deviceio_t *hw_i2c_init(uint8_t index, uint8_t addr, hw_gpio_t *sda_pin,
                           hw_gpio_t *scl_pin) {
  sys_debugf("hw", "i2c_init: index=%u addr=%u", index, addr);
  if (index >= hw_i2c_count() || !_hw_i2c_valid_addr(addr)) {
    return NULL;
  }

  hw_i2c_bus_t *bus = &_hw_i2c_bus[index];

  _sys_sync_pool_lock();

  bool bus_started_here = false;
  if (bus->refcount == 0) {
    if (!_hw_i2c_pin_matches(sda_pin, index, 0) ||
        !_hw_i2c_pin_matches(scl_pin, index, 1)) {
      _sys_sync_pool_unlock();
      return NULL;
    }

    i2c_inst_t *instance = i2c_get_instance(index);
    hw_gpio_set_mode(sda_pin, hw_gpio_i2c);
    hw_gpio_set_mode(scl_pin, hw_gpio_i2c);

    if (i2c_init(instance, HW_I2C_BAUD_RATE) == 0) {
      _sys_sync_pool_unlock();
      return NULL;
    }

    bus->instance = instance;
    bus->sda_pin = sda_pin;
    bus->scl_pin = scl_pin;
    mutex_init(&bus->lock);
    bus->owns_pins = false;
    bus_started_here = true;
  }

  // Count this device as joined now, while still under the lock, so a
  // concurrent deinit elsewhere on this bus can't see refcount drop to 0
  // and tear it down out from under us during the gap below.
  bus->refcount++;
  _sys_sync_pool_unlock();

  // _hw_deviceio_alloc_handle() takes the same lock internally for its
  // own pool - must not be called while still holding it above, or the
  // (non-reentrant) critical section would deadlock against itself.
  hw_deviceio_t *device = _hw_deviceio_alloc_handle(&_hw_i2c_ops);
  if (device != NULL) {
    hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
    ctx->bus = bus;
    ctx->addr = addr;
    return device;
  }

  // Roll back the membership claimed above.
  _sys_sync_pool_lock();
  if (--bus->refcount == 0 && bus_started_here) {
    i2c_deinit(bus->instance);
    memset(bus, 0, sizeof(*bus));
  }
  _sys_sync_pool_unlock();
  return NULL;
}

hw_deviceio_t *hw_i2c_init_device(const char *device, uint8_t addr) {
  (void)device;
  (void)addr;
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_i2c_detect(hw_deviceio_t *device, uint8_t addr) {
  if (!_hw_i2c_valid(device) || !_hw_i2c_valid_addr(addr)) {
    return false;
  }
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  hw_i2c_bus_t *bus = ctx->bus;

  mutex_enter_blocking(&bus->lock);

  // Some devices only ACK a read, others only a write, during scan-style
  // probing with no register address involved - try both.
  uint8_t probe = 0;
  if (_hw_i2c_read(bus->instance, addr, &probe, 1, false, 100) == 1) {
    mutex_exit(&bus->lock);
    return true;
  }

  probe = 0;
  bool detected =
      _hw_i2c_write(bus->instance, addr, &probe, 1, false, 100) == 1;
  mutex_exit(&bus->lock);
  return detected;
}
