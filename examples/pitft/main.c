#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

// Adafruit 2.8" PiTFT (resistive): ILI9341 display controller on the Pi's
// primary SPI bus (SCK/MOSI/MISO on GPIO11/10/9), chip-select on CE0
// (/dev/spidev0.0, GPIO8 - hardware chip-select, no separate GPIO needed).
// /DC (data/command select) is GPIO25; /RESET has no GPIO of its own on
// this board - it's generated on-board by a power-on-reset supervisor
// (see dev/ili9341.h's module note), so dev_ili9341_init() is passed NULL
// for rst_pin and falls back to the panel's own software-reset command.
#define PITFT_SPI_DEVICE "/dev/spidev0.0"
// 32MHz - the datasheet's stated max for RAM write throughput, and what
// Adafruit's own production device-tree overlay uses for this panel.
#define PITFT_SPI_BAUD 32000000
#define PITFT_DC_GPIO_BANK 0
#define PITFT_DC_GPIO_PIN 25

// RGB565: 5 bits red, 6 bits green, 5 bits blue - pure red is red maxed,
// green/blue at zero.
#define PITFT_COLOR_RED 0xF800u

int main(int argc, char *argv[]) {
  sys_init(argc, argv, 0, sys_stdio_none);
  hw_init();

  hw_deviceio_t *device =
      hw_spi_init_device(PITFT_SPI_DEVICE, PITFT_SPI_BAUD, NULL);
  if (device == NULL) {
    sys_puts("Failed to open " PITFT_SPI_DEVICE "\n");
    hw_exit();
    sys_exit();
    return 1;
  }

  // Owned but not yet configured - dev_ili9341_init() sets its mode itself.
  hw_gpio_t *dc_pin =
      hw_gpio_init(PITFT_DC_GPIO_BANK, PITFT_DC_GPIO_PIN, hw_gpio_none);

  dev_ili9341_t *ili9341 = dev_ili9341_init(device, dc_pin, NULL, NULL);
  if (ili9341 == NULL) {
    sys_puts("Failed to initialize ILI9341 display\n");
    hw_gpio_deinit(dc_pin);
    hw_deviceio_deinit(device);
    hw_exit();
    sys_exit();
    return 1;
  }

  pix_size_t size;
  dev_ili9341_size(ili9341, &size);

  uint16_t *pixels = sys_malloc((size_t)size.w * size.h * sizeof(uint16_t));
  if (pixels == NULL) {
    sys_puts("Failed to allocate framebuffer\n");
    dev_ili9341_deinit(ili9341);
    hw_gpio_deinit(dc_pin);
    hw_deviceio_deinit(device);
    hw_exit();
    sys_exit();
    return 1;
  }
  for (size_t i = 0; i < (size_t)size.w * size.h; i++) {
    pixels[i] = PITFT_COLOR_RED;
  }

  pix_bitmap_t bitmap = {
      .data = pixels,
      .size = size,
      .stride = (size_t)size.w * sizeof(uint16_t),
      .fmt = PIX_FMT_RGB565,
  };
  pix_point_t origin = {.x = 0, .y = 0};

  if (dev_ili9341_write(ili9341, origin, &bitmap)) {
    sys_puts("Screen cleared to red.\n");
  } else {
    sys_puts("Failed to write to display.\n");
  }

  sys_free(pixels);
  dev_ili9341_deinit(ili9341);
  hw_gpio_deinit(dc_pin);
  hw_deviceio_deinit(device);
  hw_exit();
  sys_exit();
  return 0;
}
