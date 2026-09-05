#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

// PiTFT capacitive touch overlay: FT6236 wired to the Raspberry Pi's
// primary I2C bus (physical pin 3 = GPIO2/SDA, pin 5 = GPIO3/SCL, exposed
// as /dev/i2c-1), with the interrupt line on GPIO24.
#define PITFT_I2C_DEVICE "/dev/i2c-1"
#define PITFT_INT_GPIO_BANK 0
#define PITFT_INT_GPIO_PIN 24

int main(int argc, char *argv[]) {
  sys_init(argc, argv, 0, sys_stdio_none);
  hw_init();

  hw_deviceio_t *device = hw_i2c_init_device(PITFT_I2C_DEVICE, DEV_FT6236_I2C_ADDR_DEFAULT);
  if (device == NULL) {
    sys_puts("Failed to open " PITFT_I2C_DEVICE "\n");
    hw_exit();
    sys_exit();
    return 1;
  }

  // Owned but not yet configured - dev_ft6236_init() sets its mode itself.
  hw_gpio_t *int_pin =
      hw_gpio_init(PITFT_INT_GPIO_BANK, PITFT_INT_GPIO_PIN, hw_gpio_none);

  dev_ft6236_t *ft6236 = dev_ft6236_init(device, int_pin, NULL);
  if (ft6236 == NULL) {
    sys_puts("Failed to initialize FT6236 touch controller\n");
    hw_gpio_deinit(int_pin);
    hw_deviceio_deinit(device);
    hw_exit();
    sys_exit();
    return 1;
  }

  sys_puts("PiTFT touch controller ready. Waiting for touches...\n");

  dev_ft6236_touch_t touches[DEV_FT6236_MAX_POINTS];
  while (true) {
    uint8_t touch_count = 0;
    if (dev_ft6236_poll(ft6236, touches, &touch_count)) {
      for (uint8_t i = 0; i < touch_count; i++) {
        if (touches[i].event == dev_ft6236_touch_up) {
          continue;
        }
        sys_printf("touch id=%u event=%u x=%u y=%u\n", touches[i].id,
                   touches[i].event, touches[i].x, touches[i].y);
      }
    }
    sys_sleep_ms(20);
  }
}
