#pragma once
#include <picofuse/dev/ft6236.h>
#include <picofuse/hw.h>
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

#define FT6236_REG_DATA_START 0x00u
#define FT6236_DATA_LENGTH 15u

#define FT6236_TOUCH_COUNT_MASK 0x0Fu
#define FT6236_TOUCH_EVENT_SHIFT 6u
#define FT6236_TOUCH_EVENT_MASK 0x03u
#define FT6236_TOUCH_POS_MASK 0x0Fu
#define FT6236_TOUCH_ID_SHIFT 4u
#define FT6236_TOUCH_ID_MASK 0x0Fu

#define FT6236_EVENT_DOWN 0u
#define FT6236_EVENT_UP 1u
#define FT6236_EVENT_CONTACT 2u
#define FT6236_EVENT_NONE 3u

// FT6X36 datasheet Table 3-5 "Power on/Reset/Wake Sequence Parameters" -
// Trst (reset pulse width) and Trsi (time to first valid report after
// resetting) minimums, both in ms. Real measured requirements, not
// arbitrary values - see dev_ft6236_init()'s own doc.
#define FT6236_RESET_PULSE_MS 5u
#define FT6236_RESET_WAIT_MS 300u

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct dev_ft6236_t {
  hw_deviceio_t *device;
  hw_gpio_t *int_pin;
  hw_gpio_t *reset_pin;
  bool irq_active_low;
  bool had_touch;
  dev_ft6236_callback_t callback;
  void *userdata;
  // Previous poll's touch state per slot, for dev_ft6236_poll()'s own
  // change detection - see its doc on why FT6236 needs this itself
  // (unlike a FIFO-backed controller). Struct-compared with memcmp(), so
  // every write to a hid_touch_t here must go through
  // _dev_ft6236_clear_touches()/_dev_ft6236_parse_frame() (both of which
  // memset() before filling fields) - never leave padding bytes
  // uninitialized, or two logically-identical values could still compare
  // unequal.
  hid_touch_t last_touches[DEV_FT6236_MAX_POINTS];
};
