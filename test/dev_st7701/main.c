#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Registered only for PICO_BOARD=presto (see test/CMakeLists.txt). These
// pins aren't exposed as board header macros the way touch/LED/I2C are -
// taken directly from the schematic (src/picofuse/dev/pimoroni/presto/
// pico_presto_schematic.pdf, sheet 2 "RP2350B chip"): GPIO26/27 are
// SPI1's hardware SCLK/TX. GPIO28 is SPI1's hardware RX, but Presto wires
// it to LCD_SPI_CS instead and drives it as a plain GPIO output (matching
// Pimoroni's own driver, which bit-bangs CS rather than using the
// peripheral's hardware CS line) - hence passing it as cs_pin below, not
// as an rx_pin (this panel has no MISO line at all).
#define PRESTO_LCD_SPI_INDEX 1u
#define PRESTO_LCD_SPI_SCK_PIN 26u
#define PRESTO_LCD_SPI_DATA_PIN 27u
#define PRESTO_LCD_SPI_CS_PIN 28u
#define PRESTO_LCD_BACKLIGHT_PIN 45u
#define PRESTO_LCD_WIDTH 480u
#define PRESTO_LCD_HEIGHT 480u
#define PRESTO_LCD_SPI_BAUD_HZ 8000000u

test_main_hw(0) {
  hw_gpio_t *sck = hw_gpio_init(0, PRESTO_LCD_SPI_SCK_PIN, hw_gpio_none);
  hw_gpio_t *tx = hw_gpio_init(0, PRESTO_LCD_SPI_DATA_PIN, hw_gpio_none);
  hw_gpio_t *cs = hw_gpio_init(0, PRESTO_LCD_SPI_CS_PIN, hw_gpio_none);
  hw_gpio_t *bl = hw_gpio_init(0, PRESTO_LCD_BACKLIGHT_PIN, hw_gpio_none);
  test_assert(sck != NULL);
  test_assert(tx != NULL);
  test_assert(cs != NULL);
  test_assert(bl != NULL);

  // No MISO line on this panel - rx_pin is NULL. bits_per_word=9 packs
  // command/data select as the 9th bit of every word (see
  // dev_st7701_init()'s own doc).
  hw_spi_config_t spi_config = {
      .cs_active_low = true,
      .mode = hw_spi_mode_0,
      .bits_per_word = 9,
  };
  hw_deviceio_t *device = hw_spi_init(PRESTO_LCD_SPI_INDEX, sck, tx, NULL, cs,
                                      PRESTO_LCD_SPI_BAUD_HZ, &spi_config);
  test_assert(device != NULL);

  pix_size_t size = {.w = PRESTO_LCD_WIDTH, .h = PRESTO_LCD_HEIGHT};
  pix_display_t *display = dev_st7701_init(device, size, bl, NULL);
  sys_printf("[dev_st7701] init: %s\n", display != NULL ? "ok" : "failed");
  test_assert(display != NULL);

  // Held, labelled steps rather than a fast continuous ramp - easier to
  // correlate what's actually visible against a printed value.
  static const uint8_t levels[] = {0,  32,  64,  96,  128,
                                   160, 192, 224, 255, 0};
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    sys_printf("[dev_st7701] backlight -> %u\n", levels[i]);
    dev_st7701_set_backlight(display, levels[i]);
    sys_sleep_ms(1500);
  }

  pix_display_deinit(display);
  hw_deviceio_deinit(device);
  hw_gpio_deinit(bl);
  hw_gpio_deinit(cs);
  hw_gpio_deinit(tx);
  hw_gpio_deinit(sck);
}
