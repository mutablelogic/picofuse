#include "../deviceio/private.h"
#include <picofuse/hw.h>
#include <picofuse/sys.h>

#include <assert.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Per-device context - each hw_spi_init_device() call opens its own fd
// bound to one device path, so (unlike Pico's index-owned-adapter model in
// src/picofuse/hw/pico/spi.c) there's no shared adapter state to protect -
// `lock` here only guards this device's own fd against concurrent callers
// on the same handle.
typedef struct hw_spi_ctx_t {
  sys_mutex_t *lock;
  int fd;
  uint32_t baud_rate;
  uint8_t bits_per_word;
} hw_spi_ctx_t;

static_assert(sizeof(hw_spi_ctx_t) <= HW_DEVICEIO_CONTEXT_SIZE,
             "hw_spi_ctx_t too large for hw_deviceio_t's embedded context");

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

/** @brief One SPI_IOC_MESSAGE request of a write followed by a read,
 * holding chip-select asserted across both - used by both hw_deviceio_xfr()
 * (write then read) and hw_deviceio_read_reg() (register byte, then the
 * bytes it returns), which share this exact shape. */
static size_t _hw_spi_write_read(int fd, uint32_t baud_rate,
                                 uint8_t bits_per_word, void *wbuf,
                                 size_t wlen, void *rbuf, size_t rlen) {
  if ((wlen == 0 && rlen == 0) || wlen > UINT32_MAX || rlen > UINT32_MAX) {
    return 0;
  }

  struct spi_ioc_transfer messages[2] = {0};
  uint32_t count = 0;

  if (wlen > 0) {
    messages[count].tx_buf = (uintptr_t)wbuf;
    messages[count].len = (uint32_t)wlen;
    messages[count].speed_hz = baud_rate;
    messages[count].bits_per_word = bits_per_word;
    messages[count].cs_change = rlen > 0 ? 0u : 1u;
    count++;
  }

  if (rlen > 0) {
    messages[count].rx_buf = (uintptr_t)rbuf;
    messages[count].len = (uint32_t)rlen;
    messages[count].speed_hz = baud_rate;
    messages[count].bits_per_word = bits_per_word;
    messages[count].cs_change = 1u;
    count++;
  }

  int ret = ioctl(fd, SPI_IOC_MESSAGE(count), messages);
  return ret == (int)(wlen + rlen) ? wlen + rlen : 0;
}

/** @brief One SPI_IOC_MESSAGE request of up to two writes, holding
 * chip-select asserted across both - used by hw_deviceio_write_reg() to
 * send the register byte followed by its data without combining the two
 * into one buffer first. */
static size_t _hw_spi_write_write(int fd, uint32_t baud_rate,
                                  uint8_t bits_per_word, const void *buf1,
                                  size_t len1, const void *buf2, size_t len2) {
  if (len1 == 0 || len1 > UINT32_MAX || len2 > UINT32_MAX) {
    return 0;
  }

  struct spi_ioc_transfer messages[2] = {0};
  messages[0].tx_buf = (uintptr_t)buf1;
  messages[0].len = (uint32_t)len1;
  messages[0].speed_hz = baud_rate;
  messages[0].bits_per_word = bits_per_word;
  messages[0].cs_change = len2 > 0 ? 0u : 1u;

  uint32_t count = 1;
  if (len2 > 0) {
    messages[1].tx_buf = (uintptr_t)buf2;
    messages[1].len = (uint32_t)len2;
    messages[1].speed_hz = baud_rate;
    messages[1].bits_per_word = bits_per_word;
    messages[1].cs_change = 1u;
    count = 2;
  }

  int ret = ioctl(fd, SPI_IOC_MESSAGE(count), messages);
  return ret == (int)(len1 + len2) ? len1 + len2 : 0;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - OPS

static size_t _hw_spi_ops_xfr(hw_deviceio_t *device, void *data, size_t tx,
                              size_t rx, uint32_t timeout_ms) {
  (void)timeout_ms; // spidev's SPI_IOC_MESSAGE ioctl has no timeout path
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
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
  size_t transferred = _hw_spi_write_read(
      ctx->fd, ctx->baud_rate, ctx->bits_per_word, bytes, tx, bytes + tx, rx);
  sys_mutex_unlock(ctx->lock);
  return transferred;
}

static size_t _hw_spi_ops_read_reg(hw_deviceio_t *device, uint8_t reg,
                                   void *data, size_t len,
                                   uint32_t timeout_ms) {
  (void)timeout_ms;
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  if (data == NULL || len == 0) {
    return 0;
  }

  if (!sys_mutex_lock(ctx->lock)) {
    return 0;
  }
  size_t transferred = _hw_spi_write_read(ctx->fd, ctx->baud_rate,
                                          ctx->bits_per_word, &reg, 1, data,
                                          len);
  sys_mutex_unlock(ctx->lock);

  return transferred == len + 1 ? len : 0;
}

static size_t _hw_spi_ops_write_reg(hw_deviceio_t *device, uint8_t reg,
                                    const void *data, size_t len,
                                    uint32_t timeout_ms) {
  (void)timeout_ms;
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  if (len > 0 && data == NULL) {
    return 0;
  }

  if (!sys_mutex_lock(ctx->lock)) {
    return 0;
  }
  size_t transferred = _hw_spi_write_write(
      ctx->fd, ctx->baud_rate, ctx->bits_per_word, &reg, 1, data, len);
  sys_mutex_unlock(ctx->lock);

  return transferred == len + 1 ? len : 0;
}

static void _hw_spi_ops_deinit(hw_deviceio_t *device) {
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  close(ctx->fd);
  sys_mutex_deinit(ctx->lock);
}

static const hw_deviceio_ops_t _hw_spi_ops = {
    .xfr = _hw_spi_ops_xfr,
    .read_reg = _hw_spi_ops_read_reg,
    .write_reg = _hw_spi_ops_write_reg,
    .deinit = _hw_spi_ops_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

uint8_t hw_spi_count(void) { return 0; }

hw_deviceio_t *hw_spi_init_default(uint32_t baud_rate,
                                   const hw_spi_config_t *config) {
  sys_debugf("hw", "spi_init_default: unsupported on this target (baud=%u)",
             baud_rate);
  (void)baud_rate;
  (void)config;
  return NULL;
}

hw_deviceio_t *hw_spi_init(uint8_t index, hw_gpio_t *sck_pin,
                          hw_gpio_t *tx_pin, hw_gpio_t *rx_pin,
                          hw_gpio_t *cs_pin, uint32_t baud_rate,
                          const hw_spi_config_t *config) {
  sys_debugf("hw", "spi_init: unsupported on this target (index=%u baud=%u)",
             index, baud_rate);
  (void)index;
  (void)sck_pin;
  (void)tx_pin;
  (void)rx_pin;
  (void)cs_pin;
  (void)baud_rate;
  (void)config;
  return NULL;
}

hw_deviceio_t *hw_spi_init_device(const char *device, uint32_t baud_rate,
                                  const hw_spi_config_t *config) {
  sys_debugf("hw", "spi_init_device: device=%s baud=%u",
             device != NULL ? device : "(null)", baud_rate);

  hw_spi_config_t settings =
      config != NULL ? *config
                    : (hw_spi_config_t){.cs_active_low = true,
                                        .mode = hw_spi_mode_0,
                                        .bits_per_word = 8};
  if (device == NULL || device[0] == '\0' || baud_rate == 0 ||
      settings.bits_per_word == 0) {
    return NULL;
  }

  int fd = open(device, O_RDWR);
  if (fd < 0) {
    return NULL;
  }

  uint8_t ioctl_mode = (uint8_t)settings.mode;
  if (!settings.cs_active_low) {
    ioctl_mode |= SPI_CS_HIGH;
  }

  if (ioctl(fd, SPI_IOC_WR_MODE, &ioctl_mode) < 0 ||
      ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &settings.bits_per_word) < 0 ||
      ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &baud_rate) < 0) {
    close(fd);
    return NULL;
  }

  sys_mutex_t *lock = sys_mutex_init();
  if (lock == NULL) {
    close(fd);
    return NULL;
  }

  hw_deviceio_t *handle = _hw_deviceio_alloc_handle(&_hw_spi_ops);
  if (handle == NULL) {
    sys_mutex_deinit(lock);
    close(fd);
    return NULL;
  }

  hw_spi_ctx_t *ctx = _hw_deviceio_context(handle);
  ctx->lock = lock;
  ctx->fd = fd;
  ctx->baud_rate = baud_rate;
  ctx->bits_per_word = settings.bits_per_word;
  return handle;
}
