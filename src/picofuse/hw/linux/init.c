#include <picofuse/hw/init.h>
#include <stdbool.h>

// Defined in gpio.c.
extern bool _hw_gpio_module_init(void);
extern void _hw_gpio_module_exit(void);

// Defined in ../led/blink.c.
extern void _hw_led_poll(void);

// Defined in ../stub/watchdog.c.
extern void _hw_watchdog_module_exit(void);
extern void _hw_watchdog_poll(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void hw_init(void) { _hw_gpio_module_init(); }

void hw_exit(void) {
  _hw_watchdog_module_exit();
  _hw_gpio_module_exit();
}

void hw_poll(void) {
  _hw_led_poll();
  _hw_watchdog_poll();
}
