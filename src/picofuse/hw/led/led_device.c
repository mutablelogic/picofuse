#include "led.h"

// LED-class sysfs (/sys/class/leds/) is a Linux kernel-driver concept -
// Pico and Darwin have no equivalent. See hw/linux/led_device.c for the
// real implementation.
hw_led_t *hw_led_init_device(const char *name) {
  (void)name;
  return NULL;
}
