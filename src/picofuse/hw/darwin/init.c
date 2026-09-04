#include <picofuse/hw/init.h>

#ifdef PICOFUSE_WIFI
// Defined in wifi.m.
extern void _hw_wifi_module_init(void);
extern void _hw_wifi_module_exit(void);
#endif

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void hw_init(void) {
#ifdef PICOFUSE_WIFI
  _hw_wifi_module_init();
#endif
}

void hw_exit(void) {
#ifdef PICOFUSE_WIFI
  _hw_wifi_module_exit();
#endif
}

void hw_poll(void) {
  // No hardware backend
}
