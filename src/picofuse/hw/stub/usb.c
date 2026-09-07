#include <picofuse/hw.h>
#include <stdint.h>

struct hw_usb_t {
  uint8_t placeholder;
};

/** Stub implementation: no USB host controller on this platform. */
hw_usb_t *hw_usb_init(void) { return NULL; }

/** Stub implementation: no USB host controller on this platform. */
void hw_usb_deinit(hw_usb_t *usb) { (void)usb; }

/** Stub implementation: no USB host controller on this platform. */
void hw_usb_set_callback(hw_usb_t *usb, hw_usb_callback_t callback,
                         void *userdata) {
  (void)usb;
  (void)callback;
  (void)userdata;
}
