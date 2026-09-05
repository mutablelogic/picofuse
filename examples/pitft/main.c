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

// @todo diagnostic: candidate switch pins -> GND on button press. The
// vendored schematic (rev D) says GPIO22/27/17/23, but this board appears
// to be an earlier revision with different wiring (believed 23/22/21-or-27/
// 18) - watching the union of both sets rather than guessing further. No
// hw_gpio_set_callback() path in this project has an automated test yet,
// so before trusting anything about GPIO24/the STMPE610, this proves edge
// detection + the callback dispatch pipeline work at all on this board,
// using real physical buttons instead of a jumper wire.
static const uint8_t _pitft_sw_pins[] = {22, 23, 21, 27, 17, 18};

// @todo diagnostic: prints every raw edge on GPIO24 and SW1-4, independent
// of dev_stmpe610's own SPI-based touch detection - lets us see whether the
// STMPE610 is toggling its INT line at all when touched, and separately
// whether GPIO edge callbacks work on this platform at all, before trusting
// any assumption about polarity or the poll() logic.
static void _pitft_gpio_callback(uint8_t bank, uint8_t pin,
                                 hw_gpio_event_t event, void *userdata) {
  (void)userdata;
  if (bank != PITFT_INT_GPIO_BANK) {
    return;
  }
  const char *edge = event == hw_gpio_rising ? "rising" : "falling";
  if (pin == PITFT_INT_GPIO_PIN) {
    sys_printf("GPIO24 %s\n", edge);
    return;
  }
  for (size_t i = 0; i < sizeof(_pitft_sw_pins); i++) {
    if (pin == _pitft_sw_pins[i]) {
      sys_printf("GPIO%u (SW%u) %s\n", pin, (unsigned)(i + 1), edge);
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

  // @todo diagnostic: hw_gpio_pullup (not hw_gpio_none) so the kernel
  // actually requests edge detection on this line for the raw-edge
  // callback below, independent of whatever mode dev_stmpe610_init()
  // would otherwise set it to. STMPE610's INT is open-drain (datasheet
  // pin table) with no external pull-up in the schematic, so it needs
  // the Pi's own internal pull-up to read a defined idle-high level.
  hw_gpio_t *int_pin =
      hw_gpio_init(PITFT_INT_GPIO_BANK, PITFT_INT_GPIO_PIN, hw_gpio_pullup);

  // @todo diagnostic: claim SW1-4 too, same reasoning as int_pin above -
  // each switch just shorts the pin to GND with no pull-up resistor of
  // its own visible in the schematic, so hw_gpio_pullup is needed here
  // for a defined idle-high level too. Leaked deliberately (no
  // hw_gpio_deinit()) - this whole block is throwaway diagnostic code
  // for one manual test run, not shipped behavior worth cleaning up on
  // every exit path.
  for (size_t i = 0; i < sizeof(_pitft_sw_pins); i++) {
    hw_gpio_init(PITFT_INT_GPIO_BANK, _pitft_sw_pins[i], hw_gpio_pullup);
  }

  hw_gpio_set_callback(_pitft_gpio_callback, NULL);

  // @todo diagnostic: NULL forces pure polling, bypassing the IRQ-pin
  // idle-skip in dev_stmpe610_poll() entirely - if touches now show up,
  // the IRQ polarity/bias guess in dev_stmpe610_init() is wrong and the
  // skip-gate is the culprit, not touch detection itself.
  dev_stmpe610_t *stmpe610 = dev_stmpe610_init(device, NULL, NULL);
  if (stmpe610 == NULL) {
    sys_puts("Failed to initialize STMPE610 touch controller\n");
    hw_gpio_deinit(int_pin);
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
