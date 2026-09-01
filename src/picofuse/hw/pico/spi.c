#include "../deviceio/private.h"
#include <assert.h>
#include <hardware/spi.h>
#include <pico/mutex.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Per-device context, embedded in each hw_deviceio_t's own scratch buffer
// (see _hw_deviceio_context()). Each device already gets its own dedicated
// CS pin, so - unlike I2C's shared-bus, shared-address model - there's no
// concept of several devices sharing one hw_deviceio_t's worth of state:
// hw_spi_init() on an index already in use just replaces whatever's there
// (deiniting it first), one open handle per index at a time.
typedef struct hw_spi_ctx_t {
  spi_inst_t *instance;
  hw_gpio_t *sck_pin;
  hw_gpio_t *tx_pin;
  hw_gpio_t *rx_pin;
  hw_gpio_t *cs_pin; // NULL if this device leaves CS unmanaged
  mutex_t lock;
  bool cs_active_low;
  bool owns_pins;
} hw_spi_ctx_t;

static_assert(sizeof(hw_spi_ctx_t) <= HW_DEVICEIO_CONTEXT_SIZE,
             "hw_spi_ctx_t too large for hw_deviceio_t's embedded context");

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Which handle (if any) currently owns each SPI adapter index - see
// hw_spi_init().
static hw_deviceio_t *_hw_spi_owner[NUM_SPIS] = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static inline bool _hw_spi_map_mode(hw_spi_mode_t mode, spi_cpol_t *cpol,
                                    spi_cpha_t *cpha) {
  switch (mode) {
  case hw_spi_mode_0:
    *cpol = SPI_CPOL_0;
    *cpha = SPI_CPHA_0;
    return true;
  case hw_spi_mode_1:
    *cpol = SPI_CPOL_0;
    *cpha = SPI_CPHA_1;
    return true;
  case hw_spi_mode_2:
    *cpol = SPI_CPOL_1;
    *cpha = SPI_CPHA_0;
    return true;
  case hw_spi_mode_3:
    *cpol = SPI_CPOL_1;
    *cpha = SPI_CPHA_1;
    return true;
  }
  return false;
}

static inline void _hw_spi_set_cs(const hw_spi_ctx_t *ctx, bool active) {
  if (ctx->cs_pin != NULL) {
    hw_gpio_set(ctx->cs_pin, ctx->cs_active_low ? !active : active);
  }
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static size_t _hw_spi_ops_xfr(hw_deviceio_t *device, void *data, size_t tx,
                              size_t rx, uint32_t timeout_ms) {
  (void)timeout_ms; // the SDK's spi_*_blocking() calls have no timeout path
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  if ((tx == 0 && rx == 0) || ((tx > 0 || rx > 0) && data == NULL)) {
    return 0;
  }

  uint8_t *bytes = data;
  size_t transferred = 0;

  mutex_enter_blocking(&ctx->lock);
  _hw_spi_set_cs(ctx, true);

  if (tx > 0 && rx > 0) {
    if (spi_write_blocking(ctx->instance, bytes, tx) == (int)tx &&
        spi_read_blocking(ctx->instance, 0x00, bytes + tx, rx) == (int)rx) {
      transferred = tx + rx;
    }
  } else if (tx > 0) {
    if (spi_write_blocking(ctx->instance, bytes, tx) == (int)tx) {
      transferred = tx;
    }
  } else {
    if (spi_read_blocking(ctx->instance, 0x00, bytes, rx) == (int)rx) {
      transferred = rx;
    }
  }

  _hw_spi_set_cs(ctx, false);
  mutex_exit(&ctx->lock);
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

  size_t result = 0;
  mutex_enter_blocking(&ctx->lock);
  _hw_spi_set_cs(ctx, true);

  if (spi_write_blocking(ctx->instance, &reg, 1) == 1 &&
      spi_read_blocking(ctx->instance, 0x00, data, len) == (int)len) {
    result = len;
  }

  _hw_spi_set_cs(ctx, false);
  mutex_exit(&ctx->lock);
  return result;
}

static size_t _hw_spi_ops_write_reg(hw_deviceio_t *device, uint8_t reg,
                                    const void *data, size_t len,
                                    uint32_t timeout_ms) {
  (void)timeout_ms;
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  if (len > 0 && data == NULL) {
    return 0;
  }

  size_t result = 0;
  mutex_enter_blocking(&ctx->lock);
  _hw_spi_set_cs(ctx, true);

  if (spi_write_blocking(ctx->instance, &reg, 1) == 1 &&
      (len == 0 || spi_write_blocking(ctx->instance, data, len) == (int)len)) {
    result = len;
  }

  _hw_spi_set_cs(ctx, false);
  mutex_exit(&ctx->lock);
  return result;
}

/** @brief True if some other currently-open SPI device also references
 * `pin`. Pico's hw_gpio_t is one shared struct per physical pin (see
 * src/picofuse/hw/pico/gpio.c's `static struct hw_gpio_t pins[...]`), not
 * a fresh allocation per hw_gpio_init() call - so releasing a pin a
 * replacement device has already taken over (see hw_spi_init()) would
 * invalidate it out from under that device too. */
static bool _hw_spi_pin_in_use(const hw_gpio_t *pin) {
  if (pin == NULL) {
    return false;
  }
  for (size_t i = 0; i < NUM_SPIS; i++) {
    if (_hw_spi_owner[i] == NULL) {
      continue;
    }
    hw_spi_ctx_t *ctx = _hw_deviceio_context(_hw_spi_owner[i]);
    if (ctx->sck_pin == pin || ctx->tx_pin == pin || ctx->rx_pin == pin ||
        ctx->cs_pin == pin) {
      return true;
    }
  }
  return false;
}

static void _hw_spi_ops_deinit(hw_deviceio_t *device) {
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);

  // Clear this device's own ownership slot first, so the pin-in-use scan
  // below doesn't see this (already-closing) device as still holding
  // them, and so a plain hw_deviceio_deinit() (not a replacement) fully
  // forgets about it.
  for (size_t i = 0; i < NUM_SPIS; i++) {
    if (_hw_spi_owner[i] == device) {
      _hw_spi_owner[i] = NULL;
      break;
    }
  }

  spi_deinit(ctx->instance);
  if (ctx->owns_pins) {
    if (ctx->cs_pin != NULL && !_hw_spi_pin_in_use(ctx->cs_pin)) {
      hw_gpio_deinit(ctx->cs_pin);
    }
    if (!_hw_spi_pin_in_use(ctx->sck_pin)) {
      hw_gpio_deinit(ctx->sck_pin);
    }
    if (!_hw_spi_pin_in_use(ctx->tx_pin)) {
      hw_gpio_deinit(ctx->tx_pin);
    }
    if (!_hw_spi_pin_in_use(ctx->rx_pin)) {
      hw_gpio_deinit(ctx->rx_pin);
    }
  }
}

static const hw_deviceio_ops_t _hw_spi_ops = {
    .xfr = _hw_spi_ops_xfr,
    .read_reg = _hw_spi_ops_read_reg,
    .write_reg = _hw_spi_ops_write_reg,
    .deinit = _hw_spi_ops_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

uint8_t hw_spi_count(void) {
  return NUM_SPIS > UINT8_MAX ? UINT8_MAX : (uint8_t)NUM_SPIS;
}

hw_deviceio_t *hw_spi_init_default(uint32_t baud_rate,
                                   const hw_spi_config_t *config) {
#if defined(PICO_DEFAULT_SPI) && defined(PICO_DEFAULT_SPI_SCK_PIN) &&         \
    defined(PICO_DEFAULT_SPI_TX_PIN) && defined(PICO_DEFAULT_SPI_RX_PIN)
  sys_debugf("hw", "spi_init_default: index=%u sck=%u tx=%u rx=%u baud=%u",
             PICO_DEFAULT_SPI, PICO_DEFAULT_SPI_SCK_PIN,
             PICO_DEFAULT_SPI_TX_PIN, PICO_DEFAULT_SPI_RX_PIN, baud_rate);

  hw_gpio_t *sck_pin = hw_gpio_init(0, PICO_DEFAULT_SPI_SCK_PIN, hw_gpio_spi);
  if (sck_pin == NULL) {
    return NULL;
  }
  hw_gpio_t *tx_pin = hw_gpio_init(0, PICO_DEFAULT_SPI_TX_PIN, hw_gpio_spi);
  if (tx_pin == NULL) {
    hw_gpio_deinit(sck_pin);
    return NULL;
  }
  hw_gpio_t *rx_pin = hw_gpio_init(0, PICO_DEFAULT_SPI_RX_PIN, hw_gpio_spi);
  if (rx_pin == NULL) {
    hw_gpio_deinit(sck_pin);
    hw_gpio_deinit(tx_pin);
    return NULL;
  }
#ifdef PICO_DEFAULT_SPI_CSN_PIN
  hw_gpio_t *cs_pin = hw_gpio_init(0, PICO_DEFAULT_SPI_CSN_PIN, hw_gpio_output);
  if (cs_pin == NULL) {
    hw_gpio_deinit(sck_pin);
    hw_gpio_deinit(tx_pin);
    hw_gpio_deinit(rx_pin);
    return NULL;
  }
#else
  hw_gpio_t *cs_pin = NULL;
#endif

  hw_deviceio_t *device = hw_spi_init(PICO_DEFAULT_SPI, sck_pin, tx_pin,
                                      rx_pin, cs_pin, baud_rate, config);
  if (device == NULL) {
    if (cs_pin != NULL) {
      hw_gpio_deinit(cs_pin);
    }
    hw_gpio_deinit(rx_pin);
    hw_gpio_deinit(tx_pin);
    hw_gpio_deinit(sck_pin);
    return NULL;
  }

  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  ctx->owns_pins = true;
  return device;
#else
  sys_debugf("hw", "spi_init_default: unsupported on this target (baud=%u)",
             baud_rate);
  (void)baud_rate;
  (void)config;
  return NULL;
#endif
}

hw_deviceio_t *hw_spi_init(uint8_t index, hw_gpio_t *sck_pin,
                           hw_gpio_t *tx_pin, hw_gpio_t *rx_pin,
                           hw_gpio_t *cs_pin, uint32_t baud_rate,
                           const hw_spi_config_t *config) {
  sys_debugf("hw", "spi_init: index=%u baud=%u", index, baud_rate);
  if (index >= hw_spi_count() || sck_pin == NULL || tx_pin == NULL ||
      rx_pin == NULL || baud_rate == 0) {
    return NULL;
  }

  hw_spi_config_t settings =
      config != NULL ? *config
                     : (hw_spi_config_t){.cs_active_low = true,
                                         .mode = hw_spi_mode_0,
                                         .bits_per_word = 8};
  if (settings.bits_per_word == 0) {
    return NULL;
  }

  spi_cpol_t cpol;
  spi_cpha_t cpha;
  if (!_hw_spi_map_mode(settings.mode, &cpol, &cpha)) {
    return NULL;
  }

  spi_inst_t *instance = spi_get_instance(index);
  hw_gpio_set_mode(sck_pin, hw_gpio_spi);
  hw_gpio_set_mode(tx_pin, hw_gpio_spi);
  hw_gpio_set_mode(rx_pin, hw_gpio_spi);
  if (cs_pin != NULL) {
    hw_gpio_set_mode(cs_pin, hw_gpio_output);
  }

  spi_init(instance, baud_rate);
  spi_set_format(instance, settings.bits_per_word, cpol, cpha, SPI_MSB_FIRST);

  hw_deviceio_t *device = _hw_deviceio_alloc_handle(&_hw_spi_ops);
  if (device == NULL) {
    spi_deinit(instance);
    return NULL;
  }

  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  ctx->instance = instance;
  ctx->sck_pin = sck_pin;
  ctx->tx_pin = tx_pin;
  ctx->rx_pin = rx_pin;
  ctx->cs_pin = cs_pin;
  mutex_init(&ctx->lock);
  ctx->cs_active_low = settings.cs_active_low;
  ctx->owns_pins = false;

  _hw_spi_set_cs(ctx, false);

  // Register the new device as this index's owner BEFORE replacing
  // whatever was there - in that order, so if the old device happens to
  // own the exact same pins (e.g. hw_spi_init_default() called twice in a
  // row), _hw_spi_ops_deinit()'s pin-reuse check (_hw_spi_pin_in_use())
  // sees this new device already holding them and skips releasing them.
  hw_deviceio_t *old_device = _hw_spi_owner[index];
  _hw_spi_owner[index] = device;
  if (old_device != NULL) {
    hw_deviceio_deinit(old_device);
  }

  return device;
}

hw_deviceio_t *hw_spi_init_device(const char *device, uint32_t baud_rate,
                                  const hw_spi_config_t *config) {
  (void)device;
  (void)baud_rate;
  (void)config;
  return NULL;
}
