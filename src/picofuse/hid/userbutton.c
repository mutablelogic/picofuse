#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register_user_button(hid_t *instance, uint16_t keycode,
                                       void *userdata) {
  // hw_gpio_init_userbutton() already knows the board's user-button pin
  // (if any) and returns it configured as a pull-up input - nothing
  // board-specific to resolve here.
  hw_gpio_t *gpio = hw_gpio_init_userbutton();
  if (gpio == NULL) {
    return NULL;
  }

  // Board user buttons are wired active-low (pressing connects the pin to
  // GND), so invert=true: a falling edge (press) reports hid_state_on and
  // a rising edge (release) reports hid_state_off
  return _hid_register_gpio(instance, gpio, keycode, true, userdata);
}
