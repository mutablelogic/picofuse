#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Pins as wired on the bench: SPI0 on GPIO18-21 (SCK/MOSI/MISO/CS), plus a
// separate DC GPIO - see the wiring notes in dev/adafruit/pitft/README.md.
// No RST pin - this PiTFT generates /RESET on-board (see dev/ili9341.h).
#define ILI9341_TEST_SCK_PIN 18
#define ILI9341_TEST_MOSI_PIN 19
#define ILI9341_TEST_MISO_PIN 20
#define ILI9341_TEST_CS_PIN 21
#define ILI9341_TEST_DC_PIN 22
#define ILI9341_TEST_BAUD_HZ 20000000u

#define ILI9341_TEST_SWATCH 32

// Diagnostic-only, not part of dev_ili9341.h's public API: a plain SPI
// write "succeeding" only means the local peripheral clocked bytes out -
// SPI has no ACK, so it says nothing about whether the panel actually
// received anything. Reading back Read Display Power Mode (0Ah) - a real
// register, not a write echo - is a much sharper check. Per the datasheet,
// the read reply is [dummy byte][status byte]; the documented reset
// default is 0x08, so this is worth reading both before and after
// dev_ili9341_init() to see whether it visibly changes (booster/sleep-out/
// display-on bits set) rather than staying stuck at the reset value.
//
// This assumes CS can stay asserted across the whole command+response,
// same as any other SPI transaction - unverified against real hardware
// until now, since dev_ili9341_write()'s command writes never needed a
// read phase at all.
#define ILI9341_DIAG_RDDPM 0x0Au

static bool _read_power_mode(hw_deviceio_t *device, hw_gpio_t *dc,
                             uint8_t *out_status) {
  uint8_t cmd = ILI9341_DIAG_RDDPM;
  hw_gpio_set(dc, false);
  if (hw_deviceio_xfr(device, &cmd, 1, 0, 0) != 1) {
    return false;
  }
  hw_gpio_set(dc, true);
  uint8_t response[2] = {0}; // [0] = dummy byte, [1] = actual status
  if (hw_deviceio_xfr(device, response, 0, sizeof(response), 0) !=
      sizeof(response)) {
    return false;
  }
  *out_status = response[1];
  return true;
}

test_main_hw(0) {
  if (hw_spi_count() == 0) {
    sys_printf("[dev_ili9341] no SPI adapter available\n");
    return;
  }

  hw_gpio_t *sck = hw_gpio_init(0, ILI9341_TEST_SCK_PIN, hw_gpio_none);
  hw_gpio_t *mosi = hw_gpio_init(0, ILI9341_TEST_MOSI_PIN, hw_gpio_none);
  hw_gpio_t *miso = hw_gpio_init(0, ILI9341_TEST_MISO_PIN, hw_gpio_none);
  hw_gpio_t *cs = hw_gpio_init(0, ILI9341_TEST_CS_PIN, hw_gpio_none);
  hw_gpio_t *dc = hw_gpio_init(0, ILI9341_TEST_DC_PIN, hw_gpio_none);
  test_assert(sck != NULL && mosi != NULL && miso != NULL && cs != NULL &&
             dc != NULL);

  hw_spi_config_t spi_config = {
      .cs_active_low = true,
      .mode = hw_spi_mode_3,
      .bits_per_word = 8,
  };
  hw_deviceio_t *device = hw_spi_init(0, sck, mosi, miso, cs,
                                      ILI9341_TEST_BAUD_HZ, &spi_config);
  test_assert(device != NULL);

  // dev_ili9341_init() sets dc's mode itself, but that's too late for this
  // pre-init diagnostic read - set it here too (redundant but harmless).
  hw_gpio_set_mode(dc, hw_gpio_output);
  uint8_t power_mode_before = 0;
  bool read_ok_before = _read_power_mode(device, dc, &power_mode_before);
  sys_printf("[dev_ili9341] RDDPM before init: ok=%u value=0x%02X\n",
             read_ok_before, power_mode_before);

  dev_ili9341_t *ili9341 = dev_ili9341_init(device, dc, NULL, NULL);
  if (ili9341 == NULL) {
    sys_printf("[dev_ili9341] dev_ili9341_init() failed\n");
    hw_deviceio_deinit(device);
    hw_gpio_deinit(dc);
    return;
  }

  uint8_t power_mode_after = 0;
  bool read_ok_after = _read_power_mode(device, dc, &power_mode_after);
  sys_printf("[dev_ili9341] RDDPM after init: ok=%u value=0x%02X\n",
             read_ok_after, power_mode_after);

  pix_size_t size = {0};
  dev_ili9341_size(ili9341, &size);
  sys_printf("[dev_ili9341] size=%ux%u\n", size.w, size.h);
  test_assert(size.w == DEV_ILI9341_WIDTH);
  test_assert(size.h == DEV_ILI9341_HEIGHT);

  // Fill a corner swatch red, so success is visible on the panel itself,
  // not just inferred from a non-crashing SPI transfer.
  static uint16_t pixels[ILI9341_TEST_SWATCH * ILI9341_TEST_SWATCH];
  for (size_t i = 0; i < ILI9341_TEST_SWATCH * ILI9341_TEST_SWATCH; i++) {
    pixels[i] = 0xF800; // RGB565 pure red
  }
  pix_bitmap_t bitmap = {
      .data = pixels,
      .size = {.w = ILI9341_TEST_SWATCH, .h = ILI9341_TEST_SWATCH},
      .stride = ILI9341_TEST_SWATCH * sizeof(uint16_t),
      .fmt = PIX_FMT_RGB565,
  };
  pix_point_t origin = {.x = 0, .y = 0};
  test_assert(dev_ili9341_write(ili9341, origin, &bitmap));

  // Out-of-bounds and wrong-format writes must be rejected, not crash.
  pix_point_t out_of_bounds = {.x = (int16_t)(DEV_ILI9341_WIDTH - 1), .y = 0};
  test_assert(dev_ili9341_write(ili9341, out_of_bounds, &bitmap) == false);

  pix_bitmap_t wrong_fmt = bitmap;
  wrong_fmt.fmt = PIX_FMT_RGB888;
  test_assert(dev_ili9341_write(ili9341, origin, &wrong_fmt) == false);

  sys_sleep_ms(2000); // leave the swatch up long enough to see it

  dev_ili9341_deinit(ili9341);
  hw_deviceio_deinit(device);
  hw_gpio_deinit(dc);
  hw_gpio_deinit(sck);
  hw_gpio_deinit(mosi);
  hw_gpio_deinit(miso);
  hw_gpio_deinit(cs);
}
