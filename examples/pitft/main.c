#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

// Adafruit 2.8" PiTFT (resistive): STMPE610 touch controller and ILI9341
// display controller share the Pi's primary SPI bus (SCK/MOSI/MISO on
// GPIO11/10/9), each on its own chip-select - display on CE0
// (/dev/spidev0.0, GPIO8), touch on CE1 (/dev/spidev0.1, GPIO7). The touch
// controller's interrupt line is on GPIO24 (GPIO25 is the display's own
// reset line, unused here).
#define PITFT_SPI_DEVICE "/dev/spidev0.1"
// 500kHz, not the datasheet's 1MHz theoretical max - matches Adafruit's
// own production device-tree overlay (pitft28-resistive-overlay.dts).
#define PITFT_SPI_BAUD 500000
#define PITFT_INT_GPIO_BANK 0
#define PITFT_INT_GPIO_PIN 24

// @todo diagnostic: every 40-pin header GPIO worth watching, excluding
// GPIO0/1 (HAT EEPROM ID - claiming these can wedge the ID EEPROM probe)
// and GPIO7-11 (the SPI bus this same program is actively using for the
// STMPE610 itself - claiming those as plain GPIO input would silently
// reprogram them away from their SPI ALT function and break that
// connection mid-run). This board's actual wiring hasn't matched any
// vendored schematic so far (switches weren't on the expected pins
// either), so rather than guess again for the touch INT line, watch
// everything else and see which pin actually moves when the screen is
// touched.
static const uint8_t _pitft_watch_pins[] = {2,  3,  4,  5,  6,  12, 13, 16,
                                            17, 18, 19, 20, 21, 22, 23, 24,
                                            25, 26, 27};

// @todo diagnostic: prints every raw edge on any watched pin, independent
// of dev_stmpe610's own SPI-based touch detection - lets us see whether
// the STMPE610 is toggling any INT line at all when touched, and
// separately whether GPIO edge callbacks work on this platform at all,
// before trusting any assumption about which pin or polarity is right.
static void _pitft_gpio_callback(uint8_t bank, uint8_t pin,
                                 hw_gpio_event_t event, void *userdata) {
  (void)userdata;
  if (bank != PITFT_INT_GPIO_BANK) {
    return;
  }
  const char *edge = event == hw_gpio_rising ? "rising" : "falling";
  for (size_t i = 0; i < sizeof(_pitft_watch_pins); i++) {
    if (pin == _pitft_watch_pins[i]) {
      sys_printf("GPIO%u %s\n", pin, edge);
      return;
    }
  }
}

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

  // @todo diagnostic: hw_gpio_pullup (not hw_gpio_none) on every watched
  // pin so the kernel actually requests edge detection on each line for
  // the raw-edge callback below, independent of whatever mode
  // dev_stmpe610_init() would otherwise set GPIO24 to. Pull-up because
  // STMPE610's INT is open-drain (datasheet pin table) with no external
  // pull-up in the schematic, and the switch pins short straight to GND
  // with no pull-up of their own either - both need the Pi's internal
  // pull-up for a defined idle-high level. Leaked deliberately (no
  // hw_gpio_deinit()) - this whole block is throwaway diagnostic code for
  // one manual test run, not shipped behavior worth cleaning up on every
  // exit path.
  for (size_t i = 0; i < sizeof(_pitft_watch_pins); i++) {
    hw_gpio_init(PITFT_INT_GPIO_BANK, _pitft_watch_pins[i], hw_gpio_pullup);
  }

  hw_gpio_set_callback(_pitft_gpio_callback, NULL);

  // @todo diagnostic: NULL forces pure polling, bypassing the IRQ-pin
  // idle-skip in dev_stmpe610_poll() entirely - if touches now show up,
  // the IRQ polarity/bias guess in dev_stmpe610_init() is wrong and the
  // skip-gate is the culprit, not touch detection itself.
  dev_stmpe610_t *stmpe610 = dev_stmpe610_init(device, NULL, NULL);
  if (stmpe610 == NULL) {
    sys_puts("Failed to initialize STMPE610 touch controller\n");
    hw_deviceio_deinit(device);
    hw_exit();
    sys_exit();
    return 1;
  }

  sys_puts("PiTFT touch controller ready. Waiting for touches...\n");

  while (true) {
    dev_stmpe610_touch_t touch;
    if (dev_stmpe610_poll(stmpe610, &touch) &&
        touch.event != dev_stmpe610_touch_up) {
      sys_printf("touch event=%u x=%u y=%u z=%u\n", touch.event, touch.x,
                 touch.y, touch.z);
    }
    sys_sleep_ms(20);
  }
}
