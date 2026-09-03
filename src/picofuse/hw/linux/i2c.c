#include "../deviceio/private.h"
#include <picofuse/hw.h>
#include <picofuse/sys.h>

#include <assert.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Per-device context
typedef struct hw_i2c_ctx_t {
  sys_mutex_t *lock;
  int fd;
  uint8_t addr;
} hw_i2c_ctx_t;

static_assert(sizeof(hw_i2c_ctx_t) <= HW_DEVICEIO_CONTEXT_SIZE,
              "hw_i2c_ctx_t too large for hw_deviceio_t's embedded context");

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

/** @brief One I2C_RDWR request of up to two messages (an optional write
 * followed by an optional read), addressed to `addr` directly - Linux's
 * i2c_msg carries its own address per message, so unlike the
 * ioctl(I2C_SLAVE, ...) model there's no need to bind an address to the fd
 * first, and a single fd can freely address different devices on the same
 * bus (see hw_i2c_detect()). */
static size_t _hw_i2c_transfer(int fd, uint8_t addr, void *wbuf, size_t wlen,
                               void *rbuf, size_t rlen) {
  if ((wlen == 0 && rlen == 0) || wlen > UINT16_MAX || rlen > UINT16_MAX) {
    return 0;
  }

  struct i2c_msg messages[2] = {0};
  uint32_t count = 0;

  if (wlen > 0) {
    messages[count].addr = addr;
    messages[count].flags = 0;
    messages[count].len = (uint16_t)wlen;
    messages[count].buf = wbuf;
    count++;
  }

  if (rlen > 0) {
    messages[count].addr = addr;
    messages[count].flags = I2C_M_RD;
    messages[count].len = (uint16_t)rlen;
    messages[count].buf = rbuf;
    count++;
  }

  struct i2c_rdwr_ioctl_data request = {.msgs = messages, .nmsgs = count};
  return ioctl(fd, I2C_RDWR, &request) == (int)count ? wlen + rlen : 0;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - OPS

static size_t _hw_i2c_ops_xfr(hw_deviceio_t *device, void *data, size_t tx,
                              size_t rx, uint32_t timeout_ms) {
  (void)timeout_ms; // Linux's I2C_RDWR ioctl has no per-transfer timeout
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  if (tx == 0 && rx == 0) {
    return 0;
  }
  if ((tx > 0 || rx > 0) && data == NULL) {
    return 0;
  }

  uint8_t *bytes = data;
  if (!sys_mutex_lock(ctx->lock)) {
    return 0;
  }
  size_t transferred =
      _hw_i2c_transfer(ctx->fd, ctx->addr, bytes, tx, bytes + tx, rx);
  sys_mutex_unlock(ctx->lock);
  return transferred;
}

static size_t _hw_i2c_ops_read_reg(hw_deviceio_t *device, uint8_t reg,
                                   void *data, size_t len,
                                   uint32_t timeout_ms) {
  (void)timeout_ms;
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  if (data == NULL || len == 0) {
    return 0;
  }

  if (!sys_mutex_lock(ctx->lock)) {
    return 0;
  }
  size_t transferred = _hw_i2c_transfer(ctx->fd, ctx->addr, &reg, 1, data, len);
  sys_mutex_unlock(ctx->lock);

  return transferred == len + 1 ? len : 0;
}

static size_t _hw_i2c_ops_write_reg(hw_deviceio_t *device, uint8_t reg,
                                    const void *data, size_t len,
                                    uint32_t timeout_ms) {
  (void)timeout_ms;
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

  if (!sys_mutex_lock(ctx->lock)) {
    if (use_heap) {
      sys_free(buffer);
    }
    return 0;
  }
  size_t transferred =
      _hw_i2c_transfer(ctx->fd, ctx->addr, buffer, total_len, NULL, 0);
  sys_mutex_unlock(ctx->lock);

  if (use_heap) {
    sys_free(buffer);
  }

  return transferred == total_len ? len : 0;
}

static void _hw_i2c_ops_deinit(hw_deviceio_t *device) {
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);
  close(ctx->fd);
  sys_mutex_deinit(ctx->lock);
}

static const hw_deviceio_ops_t _hw_i2c_ops = {
    .xfr = _hw_i2c_ops_xfr,
    .read_reg = _hw_i2c_ops_read_reg,
    .write_reg = _hw_i2c_ops_write_reg,
    .deinit = _hw_i2c_ops_deinit,
};

/** @brief True if `device` was constructed by hw_i2c_init_device() - needed
 * by hw_i2c_detect(), which takes an hw_deviceio_t* directly rather than
 * going through hw_deviceio_*()'s own dispatch. */
static inline bool _hw_i2c_valid(const hw_deviceio_t *device) {
  return device != NULL && _hw_deviceio_ops(device) == &_hw_i2c_ops;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

uint8_t hw_i2c_count(void) { return 0; }

hw_deviceio_t *hw_i2c_init_default(uint8_t addr) {
  sys_debugf("hw", "i2c_init_default: unsupported on this target (addr=%u)",
             addr);
  (void)addr;
  return NULL;
}

hw_deviceio_t *hw_i2c_init(uint8_t index, uint8_t addr, hw_gpio_t *sda_pin,
                           hw_gpio_t *scl_pin) {
  sys_debugf("hw", "i2c_init: unsupported on this target (index=%u addr=%u)",
             index, addr);
  (void)index;
  (void)addr;
  (void)sda_pin;
  (void)scl_pin;
  return NULL;
}

hw_deviceio_t *hw_i2c_init_device(const char *device, uint8_t addr) {
  sys_debugf("hw", "i2c_init_device: device=%s addr=%u",
             device != NULL ? device : "(null)", addr);
  if (device == NULL || device[0] == '\0' || !_hw_i2c_valid_addr(addr)) {
    return NULL;
  }

  int fd = open(device, O_RDWR);
  if (fd < 0) {
    return NULL;
  }

  sys_mutex_t *lock = sys_mutex_init();
  if (lock == NULL) {
    close(fd);
    return NULL;
  }

  hw_deviceio_t *handle = _hw_deviceio_alloc_handle(&_hw_i2c_ops, hw_deviceio_i2c);
  if (handle == NULL) {
    sys_mutex_deinit(lock);
    close(fd);
    return NULL;
  }

  hw_i2c_ctx_t *ctx = _hw_deviceio_context(handle);
  ctx->lock = lock;
  ctx->fd = fd;
  ctx->addr = addr;
  return handle;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_i2c_detect(hw_deviceio_t *device, uint8_t addr) {
  if (!_hw_i2c_valid(device) || !_hw_i2c_valid_addr(addr)) {
    return false;
  }
  hw_i2c_ctx_t *ctx = _hw_deviceio_context(device);

  if (!sys_mutex_lock(ctx->lock)) {
    return false;
  }

  // Some devices only ACK a read, others only a write, during scan-style
  // probing with no register address involved - try both.
  uint8_t probe = 0;
  bool detected = _hw_i2c_transfer(ctx->fd, addr, NULL, 0, &probe, 1) == 1;
  if (!detected) {
    probe = 0;
    detected = _hw_i2c_transfer(ctx->fd, addr, &probe, 1, NULL, 0) == 1;
  }

  sys_mutex_unlock(ctx->lock);
  return detected;
}
