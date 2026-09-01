#include <picofuse/hw/init.h>
#include <stdbool.h>

// Defined in gpio.c.
extern bool _hw_gpio_module_init(void);
extern void _hw_gpio_module_exit(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void hw_init(void) { _hw_gpio_module_init(); }

void hw_exit(void) { _hw_gpio_module_exit(); }

void hw_poll(void) {
  // No hardware backend
}
