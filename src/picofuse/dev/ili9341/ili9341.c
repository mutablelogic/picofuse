#include "private.h"
#include <picofuse/dev/ili9341.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct dev_ili9341_t {
  hw_deviceio_t *device;
  hw_gpio_t *dc_pin;
  hw_gpio_t *rst_pin;
  uint16_t width;
  uint16_t height;
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Issues one command byte (/DC low) followed by its argument bytes, if any
// (/DC high) - two separate transfers, each its own chip-select pulse (see
// the module doc's note on why that's fine for this controller).
static bool _ili9341_write_command(dev_ili9341_t *ili9341, uint8_t cmd,
                                   const uint8_t *args, size_t argc) {
  hw_gpio_set(ili9341->dc_pin, false);
  if (hw_deviceio_xfr(ili9341->device, &cmd, 1, 0, 0) != 1) {
    return false;
  }
  if (argc == 0) {
    return true;
  }
  hw_gpio_set(ili9341->dc_pin, true);
  return hw_deviceio_xfr(ili9341->device, (void *)args, argc, 0, 0) == argc;
}

static void _ili9341_reset(dev_ili9341_t *ili9341) {
  if (ili9341->rst_pin != NULL) {
    hw_gpio_set(ili9341->rst_pin, false);
    sys_sleep_ms(ILI9341_RESET_PULSE_MS);
    hw_gpio_set(ili9341->rst_pin, true);
    sys_sleep_ms(ILI9341_RESET_SETTLE_MS);
    return;
  }
  _ili9341_write_command(ili9341, ILI9341_SWRESET, NULL, 0);
  sys_sleep_ms(ILI9341_RESET_SETTLE_MS);
}

static bool _ili9341_run_initcmd(dev_ili9341_t *ili9341) {
  const uint8_t *addr = _ili9341_initcmd;
  while (*addr != 0) {
    uint8_t cmd = *addr++;
    uint8_t raw = *addr++;
    uint8_t argc = raw & ILI9341_CMD_ARGC_MASK;

    if (!_ili9341_write_command(ili9341, cmd, addr, argc)) {
      return false;
    }
    addr += argc;

    if (raw & ILI9341_CMD_DELAY_FLAG) {
      sys_sleep_ms(ILI9341_CMD_DELAY_MS);
    }
  }
  return true;
}

// Resolves a 0/90/180/270 degree rotation to its Memory Access Control
// (36h) bits and the resulting logical width/height. Returns false for
// anything else, so dev_ili9341_init() can reject it outright.
static bool _ili9341_resolve_rotation(uint16_t rotation, uint8_t *out_madctl,
                                      uint16_t *out_width,
                                      uint16_t *out_height) {
  switch (rotation) {
  case 0:
    *out_madctl = ILI9341_MADCTL_MX | ILI9341_MADCTL_BGR;
    *out_width = DEV_ILI9341_WIDTH;
    *out_height = DEV_ILI9341_HEIGHT;
    return true;
  case 90:
    *out_madctl = ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR;
    *out_width = DEV_ILI9341_HEIGHT;
    *out_height = DEV_ILI9341_WIDTH;
    return true;
  case 180:
    *out_madctl = ILI9341_MADCTL_MY | ILI9341_MADCTL_BGR;
    *out_width = DEV_ILI9341_WIDTH;
    *out_height = DEV_ILI9341_HEIGHT;
    return true;
  case 270:
    *out_madctl = ILI9341_MADCTL_MX | ILI9341_MADCTL_MY | ILI9341_MADCTL_MV |
                 ILI9341_MADCTL_BGR;
    *out_width = DEV_ILI9341_HEIGHT;
    *out_height = DEV_ILI9341_WIDTH;
    return true;
  default:
    return false;
  }
}

// Sets the GRAM address window and issues Memory Write (2Ch), leaving /DC
// high and ready for the pixel data that follows.
static bool _ili9341_set_window(dev_ili9341_t *ili9341, uint16_t x,
                                uint16_t y, uint16_t w, uint16_t h) {
  uint16_t x_end = x + w - 1;
  uint16_t y_end = y + h - 1;
  uint8_t xa[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x_end >> 8),
                   (uint8_t)x_end};
  uint8_t ya[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y_end >> 8),
                   (uint8_t)y_end};

  return _ili9341_write_command(ili9341, ILI9341_CASET, xa, sizeof(xa)) &&
        _ili9341_write_command(ili9341, ILI9341_PASET, ya, sizeof(ya)) &&
        _ili9341_write_command(ili9341, ILI9341_RAMWR, NULL, 0);
}

// Streams bitmap's pixels out, converting each one from native byte order
// to the wire's big-endian order through a bounded stack buffer - the
// bitmap can be arbitrarily large (a full-screen update), so this can't
// just byte-swap the whole thing into one heap/stack buffer up front.
//
// 1024 pixels (2048 bytes) stays under half the Linux spidev driver's own
// default per-transfer buffer size (module parameter `bufsiz`, 4096 on an
// unmodified install), leaving headroom rather than sitting right at the
// limit, while keeping the on-stack chunk buffer small enough to still be
// reasonable on a constrained target - this driver's other dependencies
// (hw_gpio, hw_deviceio_xfr) are platform-generic even though
// hw_spi_init_device() itself is Linux-only today. Each chunk is one
// ioctl() syscall, so this is still an 8x reduction in syscalls per
// screen versus the 128-pixel chunking this replaced.
#define ILI9341_CHUNK_PIXELS 1024u

static bool _ili9341_write_pixels(dev_ili9341_t *ili9341,
                                  const pix_bitmap_t *bitmap) {
  hw_gpio_set(ili9341->dc_pin, true);

  uint8_t chunk[ILI9341_CHUNK_PIXELS * 2];
  for (uint16_t row = 0; row < bitmap->size.h; row++) {
    const uint16_t *src =
        (const uint16_t *)((const uint8_t *)bitmap->data +
                           (size_t)row * bitmap->stride);
    uint16_t remaining = bitmap->size.w;
    while (remaining > 0) {
      uint16_t n = remaining < ILI9341_CHUNK_PIXELS ? remaining
                                                    : ILI9341_CHUNK_PIXELS;
      for (uint16_t i = 0; i < n; i++) {
        uint16_t pixel = src[i];
        chunk[i * 2] = (uint8_t)(pixel >> 8);
        chunk[i * 2 + 1] = (uint8_t)pixel;
      }
      if (hw_deviceio_xfr(ili9341->device, chunk, (size_t)n * 2, 0, 0) !=
          (size_t)n * 2) {
        return false;
      }
      src += n;
      remaining -= n;
    }
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

void dev_ili9341_default_config(dev_ili9341_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->rotation = 0;
}

dev_ili9341_t *dev_ili9341_init(hw_deviceio_t *device, hw_gpio_t *dc_pin,
                                hw_gpio_t *rst_pin,
                                const dev_ili9341_config_t *config) {
  if (device == NULL || dc_pin == NULL) {
    return NULL;
  }

  dev_ili9341_config_t resolved;
  dev_ili9341_default_config(&resolved);
  if (config != NULL) {
    resolved = *config;
  }

  uint8_t madctl = 0;
  uint16_t width = 0, height = 0;
  if (!_ili9341_resolve_rotation(resolved.rotation, &madctl, &width,
                                 &height)) {
    return NULL;
  }

  dev_ili9341_t *ili9341 = sys_calloc(1, sizeof(*ili9341));
  if (ili9341 == NULL) {
    return NULL;
  }

  ili9341->device = device;
  ili9341->dc_pin = dc_pin;
  ili9341->rst_pin = rst_pin;

  hw_gpio_set_mode(dc_pin, hw_gpio_output);
  if (rst_pin != NULL) {
    hw_gpio_set_mode(rst_pin, hw_gpio_output);
    hw_gpio_set(rst_pin, true);
  }

  _ili9341_reset(ili9341);

  if (!_ili9341_run_initcmd(ili9341) ||
      !_ili9341_write_command(ili9341, ILI9341_MADCTL, &madctl, 1)) {
    sys_free(ili9341);
    return NULL;
  }

  ili9341->width = width;
  ili9341->height = height;
  return ili9341;
}

void dev_ili9341_deinit(dev_ili9341_t *ili9341) {
  if (ili9341 == NULL) {
    return;
  }
  sys_free(ili9341);
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

void dev_ili9341_size(const dev_ili9341_t *ili9341, pix_size_t *out_size) {
  if (ili9341 == NULL || out_size == NULL) {
    return;
  }
  out_size->w = ili9341->width;
  out_size->h = ili9341->height;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool dev_ili9341_write(dev_ili9341_t *ili9341, pix_point_t origin,
                       const pix_bitmap_t *bitmap) {
  if (ili9341 == NULL || bitmap == NULL || bitmap->fmt != PIX_FMT_RGB565 ||
      bitmap->data == NULL) {
    return false;
  }
  if (origin.x < 0 || origin.y < 0 || bitmap->size.w == 0 ||
      bitmap->size.h == 0) {
    return false;
  }

  uint32_t x_end = (uint32_t)origin.x + bitmap->size.w;
  uint32_t y_end = (uint32_t)origin.y + bitmap->size.h;
  if (x_end > ili9341->width || y_end > ili9341->height) {
    return false;
  }

  if (!_ili9341_set_window(ili9341, (uint16_t)origin.x, (uint16_t)origin.y,
                           bitmap->size.w, bitmap->size.h)) {
    return false;
  }
  return _ili9341_write_pixels(ili9341, bitmap);
}
