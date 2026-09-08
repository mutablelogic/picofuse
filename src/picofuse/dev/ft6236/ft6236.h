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

// Longest dev_ft6236_poll() is ever allowed to go without a real I2C
// read while an interrupt pin is configured, regardless of what
// dev_ft6236_irq_active() says - see its own doc on why relying on that
// alone can miss a touch indefinitely. Several multiples of the Active
// Mode frame period (~16.7ms, see FT6236_HID_MIN_POLLING_INTERVAL_MS's
// own doc in dev/ft6236/hid.c) rather than exactly one, so the usual
// per-poll IRQ check still does almost all of the real work - this is
// only the fallback for the case it misses.
#define FT6236_IRQ_RECONCILE_MS 100u

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct dev_ft6236_t {
  hw_deviceio_t *device;
  hw_gpio_t *int_pin;
  hw_gpio_t *reset_pin;
  bool irq_active_low;
  bool had_touch;
  uint64_t last_read_ms; // 0 until the first real read - see
                        // FT6236_IRQ_RECONCILE_MS's own doc
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
