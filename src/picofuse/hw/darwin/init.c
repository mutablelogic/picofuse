#include <picofuse/hw/init.h>

#ifdef PICOFUSE_WIFI
// Defined in wifi.m.
extern void _hw_wifi_module_init(void);
extern void _hw_wifi_module_exit(void);
#endif

// Defined in ../led/blink.c.
extern void _hw_led_poll(void);

// Defined in ../stub/watchdog.c.
extern void _hw_watchdog_module_exit(void);
extern void _hw_watchdog_poll(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void hw_init(void) {
#ifdef PICOFUSE_WIFI
  _hw_wifi_module_init();
#endif
}

void hw_exit(void) {
  _hw_watchdog_module_exit();
#ifdef PICOFUSE_WIFI
  _hw_wifi_module_exit();
#endif
}

void hw_poll(void) {
  _hw_led_poll();
  _hw_watchdog_poll();
}
