#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

// Adafruit 2.8" PiTFT (resistive): STMPE610 touch controller and ILI9341
// display controller share the Pi's primary SPI bus (SCK/MOSI/MISO on
// GPIO11/10/9), each on its own chip-select - display on CE0
// (/dev/spidev0.0, GPIO8), touch on CE1 (/dev/spidev0.1, GPIO7). The touch
// controller's interrupt line is on GPIO24. GPIO25 is the display's /DC
// pin (see examples/pitft/main.c), unused here.
#define PITFT_SPI_DEVICE "/dev/spidev0.1"
// 1MHz - the datasheet's stated max, and what both Adafruit's Arduino and
// CircuitPython reference drivers default to.
#define PITFT_SPI_BAUD 1000000
#define PITFT_INT_GPIO_BANK 0
#define PITFT_INT_GPIO_PIN 24

static void pitft_touch_callback(dev_stmpe610_t *stmpe610,
                                 const dev_stmpe610_touch_t *touch,
                                 void *userdata) {
  (void)stmpe610;
  (void)userdata;
  sys_printf("touch event=%u x=%u y=%u z=%u\n", touch->event, touch->x,
             touch->y, touch->z);
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

  // Owned but not yet configured - dev_stmpe610_init() sets its mode itself.
  hw_gpio_t *int_pin =
      hw_gpio_init(PITFT_INT_GPIO_BANK, PITFT_INT_GPIO_PIN, hw_gpio_none);

  dev_stmpe610_t *stmpe610 =
      dev_stmpe610_init(device, int_pin, pitft_touch_callback, NULL, NULL);
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
    dev_stmpe610_poll(stmpe610);
    sys_sleep_ms(20);
  }
}
