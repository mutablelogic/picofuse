#include <picofuse/hw.h>

// Weak fallbacks used when picofuse-hw is not linked into the final binary,
// so picofuse-app can call hw_init()/hw_exit()/hw_poll()/hw_led_init_default()/
// hw_led_deinit() without forcing a hard dependency on the hw module. The
// real implementation (src/picofuse/hw/<platform>/init.c, led_default.c)
// defines strong symbols of the same names, which the linker prefers over
// these whenever it is present in the same link.
//
// Unlike picofuse-sys's own app module, no hw_gpio_*() fallbacks are needed
// here - picofuse-hid already hard-depends on picofuse-hw (see
// src/picofuse/hid/CMakeLists.txt), so a "picofuse-hid without picofuse-hw"
// link can never happen in the first place.

__attribute__((weak)) void hw_init(void) {}

__attribute__((weak)) void hw_exit(void) {}

__attribute__((weak)) void hw_poll(void) {}

__attribute__((weak)) hw_led_t *hw_led_init_default(void) { return NULL; }

__attribute__((weak)) void hw_led_deinit(hw_led_t *led) { (void)led; }
