#include <picofuse/hid.h>
#include <stddef.h>

// hw_usb_*() doesn't exist anywhere in this codebase yet - no USB host
// backend to initialize, so this always fails, matching
// hid_register_usb()'s own documented "or NULL on failure (for example,
// if the platform has no USB host controller support built in)" case.
// Replace this once a real hw_usb_init()/hw_usb_t backend lands.
hid_device_t *hid_register_usb(hid_t *instance) {
  (void)instance;
  return NULL;
}
