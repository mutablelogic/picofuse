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
// 80MHz - Adafruit's own Arduino driver (Adafruit_ILI9341.cpp) defaults
// to this exact value for "RASPI" specifically, well above the 32MHz the
// device-tree overlay used (that overlay drives the panel through the
// Linux fbtft framebuffer driver, not hand-tuned for throughput the way
// a direct SPI client can be).
#define PITFT_SPI_BAUD 80000000
#define PITFT_DC_GPIO_BANK 0
#define PITFT_DC_GPIO_PIN 25

// RGB565: 5 bits red, 6 bits green, 5 bits blue.
static const uint16_t _pitft_colors[] = {
    0xF800u, // red
    0x07E0u, // green
    0x001Fu, // blue
    0xFFFFu, // white
    0x0000u, // black
};

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

  size_t pixel_count = (size_t)size.w * size.h;
  // 2 bytes/pixel, big-endian (MSB first) - dev_ili9341_write() sends
  // this buffer as-is, with no conversion of its own.
  uint8_t *pixels = sys_malloc(pixel_count * 2);
  if (pixels == NULL) {
    sys_puts("Failed to allocate framebuffer\n");
    dev_ili9341_deinit(ili9341);
    hw_gpio_deinit(dc_pin);
    hw_deviceio_deinit(device);
    hw_exit();
    sys_exit();
    return 1;
  }
  pix_bitmap_t bitmap = {
      .data = pixels,
      .size = size,
      .stride = (size_t)size.w * 2,
      .fmt = PIX_FMT_RGB565,
  };
  pix_point_t origin = {.x = 0, .y = 0};

  while (true) {
    for (size_t c = 0; c < sizeof(_pitft_colors) / sizeof(_pitft_colors[0]);
        c++) {
      uint16_t color = _pitft_colors[c];
      uint8_t hi = (uint8_t)(color >> 8);
      uint8_t lo = (uint8_t)color;
      for (size_t i = 0; i < pixel_count; i++) {
        pixels[i * 2] = hi;
        pixels[i * 2 + 1] = lo;
      }
      uint64_t start_ms = sys_timestamp_ms();
      bool ok = dev_ili9341_write(ili9341, origin, &bitmap);
      uint64_t elapsed_ms = sys_timestamp_ms() - start_ms;

      if (!ok) {
        sys_puts("Failed to write to display.\n");
      } else {
        double fps = elapsed_ms > 0 ? 1000.0 / (double)elapsed_ms : 0.0;
        sys_printf("full-screen write: %llu ms (%.1f fps)\n",
                   (unsigned long long)elapsed_ms, fps);
      }
      sys_sleep_ms(1000);
    }
  }
}
