#include <picofuse/hw/init.h>

// Defined in wifi.m.
extern void _hw_wifi_module_init(void);
extern void _hw_wifi_module_exit(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void hw_init(void) { _hw_wifi_module_init(); }

void hw_exit(void) { _hw_wifi_module_exit(); }

void hw_poll(void) {
  // No hardware backend
}
