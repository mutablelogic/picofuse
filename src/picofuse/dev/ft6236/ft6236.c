#include "ft6236.h"
#include <picofuse/dev/ft6236.h>
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static void
_dev_ft6236_clear_touches(hid_touch_t touches[DEV_FT6236_MAX_POINTS],
                          uint8_t *out_touch_count) {
  if (touches != NULL) {
    memset(touches, 0, sizeof(hid_touch_t) * DEV_FT6236_MAX_POINTS);
    for (size_t i = 0; i < DEV_FT6236_MAX_POINTS; i++) {
      touches[i].state = hid_state_off;
      touches[i].slot = (uint8_t)i;
    }
  }
  if (out_touch_count != NULL) {
    *out_touch_count = 0u;
  }
}

// A fresh contact is hid_state_on; an existing one that's still down but
// moved is hid_state_repeat, not hid_state_on again - see
// dev_ft6236_poll()'s own doc.
static hid_state_t _dev_ft6236_map_event(uint8_t event_code) {
  switch (event_code) {
  case FT6236_EVENT_DOWN:
    return hid_state_on;
  case FT6236_EVENT_CONTACT:
    return hid_state_repeat;
  case FT6236_EVENT_UP:
  case FT6236_EVENT_NONE:
  default:
    return hid_state_off;
  }
}

static bool _dev_ft6236_read_frame(dev_ft6236_t *ft6236, uint8_t *buffer,
                                   size_t length) {
  if (length < FT6236_DATA_LENGTH) {
    return false;
  }
  return hw_deviceio_read_reg(ft6236->device, FT6236_REG_DATA_START, buffer,
                              FT6236_DATA_LENGTH, 0u) == FT6236_DATA_LENGTH;
}

// FT6X36's own track ID is a 4-bit field (0-15, 0xF reserved invalid) -
// not guaranteed to stay within 0..DEV_FT6236_MAX_POINTS-1 the way this
// driver's array is indexed, and not guaranteed to be reported at the
// same position within a frame from one poll to the next either. Mapping
// an out-of-range ID to its report-order index (the previous approach)
// broke on both counts: two simultaneously active contacts could
// collide onto the same array slot whenever an out-of-range ID and an
// in-range one landed on the same index across different frames -
// silently overwriting one contact's data with the other's - and a
// single contact's own assigned slot could migrate between polls purely
// because report order changed, with nothing tying it back to the same
// physical finger.
//
// This instead remembers which slot each hardware ID currently occupies
// (dev_ft6236_t::slot_track_id) across polls: an ID already tracked
// reuses its own slot; a new one claims the first free slot.
// _dev_ft6236_parse_frame() frees the mapping for any slot that isn't
// reported active in this frame's own set of IDs, so a lifted contact's
// slot becomes available again for whatever claims it next.
static uint8_t _dev_ft6236_resolve_slot(dev_ft6236_t *ft6236,
                                        uint8_t touch_id) {
  for (uint8_t i = 0; i < DEV_FT6236_MAX_POINTS; i++) {
    if (ft6236->slot_track_id[i] == touch_id) {
      return i;
    }
  }
  for (uint8_t i = 0; i < DEV_FT6236_MAX_POINTS; i++) {
    if (ft6236->slot_track_id[i] == FT6236_TRACK_ID_NONE) {
      ft6236->slot_track_id[i] = touch_id;
      return i;
    }
  }
  // Unreachable in practice: count is already clamped to
  // DEV_FT6236_MAX_POINTS, so there are never more active IDs in one
  // frame than there are slots to hold them. Falls back to slot 0 rather
  // than an out-of-bounds access if that invariant is ever wrong.
  return 0;
}

static void
_dev_ft6236_parse_frame(dev_ft6236_t *ft6236, const uint8_t *buffer,
                        hid_touch_t touches[DEV_FT6236_MAX_POINTS],
                        uint8_t *out_touch_count) {
  _dev_ft6236_clear_touches(touches, out_touch_count);
  if (buffer == NULL || touches == NULL) {
    return;
  }

  uint8_t count = buffer[2] & FT6236_TOUCH_COUNT_MASK;
  if (count > DEV_FT6236_MAX_POINTS) {
    count = DEV_FT6236_MAX_POINTS;
  }

  // Which array slots this frame actually reports active, so any
  // slot_track_id[] entry left over afterward (a slot not in this set)
  // can be freed - see _dev_ft6236_resolve_slot()'s own doc.
  bool slot_seen[DEV_FT6236_MAX_POINTS] = {0};

  for (uint8_t index = 0u; index < count; index++) {
    const uint8_t *point = &buffer[3u + ((size_t)index * 6u)];
    uint8_t touch_id =
        (point[2] >> FT6236_TOUCH_ID_SHIFT) & FT6236_TOUCH_ID_MASK;
    uint8_t slot = _dev_ft6236_resolve_slot(ft6236, touch_id);
    slot_seen[slot] = true;

    hid_state_t state = _dev_ft6236_map_event(
        (point[0] >> FT6236_TOUCH_EVENT_SHIFT) & FT6236_TOUCH_EVENT_MASK);

    touches[slot].state = state;
    touches[slot].slot = slot;
    touches[slot].point.x =
        (int16_t)(((uint16_t)(point[0] & FT6236_TOUCH_POS_MASK) << 8) |
                  point[1]);
    touches[slot].point.y =
        (int16_t)(((uint16_t)(point[2] & FT6236_TOUCH_POS_MASK) << 8) |
                  point[3]);
    if (state != hid_state_off && out_touch_count != NULL) {
      (*out_touch_count)++;
    }
  }

  for (uint8_t i = 0; i < DEV_FT6236_MAX_POINTS; i++) {
    if (!slot_seen[i]) {
      ft6236->slot_track_id[i] = FT6236_TRACK_ID_NONE;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

void dev_ft6236_default_config(dev_ft6236_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->irq_active_low = true;
  config->int_pin = NULL;
  config->reset_pin = NULL;
}

dev_ft6236_t *dev_ft6236_init(hw_deviceio_t *device,
                              const dev_ft6236_config_t *config) {
  if (device == NULL) {
    return NULL;
  }

  dev_ft6236_config_t resolved;
  dev_ft6236_default_config(&resolved);
  if (config != NULL) {
    resolved = *config;
  }

  dev_ft6236_t *ft6236 = sys_calloc(1, sizeof(*ft6236));
  if (ft6236 == NULL) {
    return NULL;
  }

  ft6236->device = device;
  ft6236->int_pin = resolved.int_pin;
  ft6236->reset_pin = resolved.reset_pin;
  ft6236->irq_active_low = resolved.irq_active_low;
  _dev_ft6236_clear_touches(ft6236->last_touches, NULL);
  // sys_calloc() zeroes this, but 0 is a real track ID - every slot
  // must start explicitly free, not implicitly "assigned to ID 0".
  memset(ft6236->slot_track_id, FT6236_TRACK_ID_NONE,
        sizeof(ft6236->slot_track_id));

  if (ft6236->int_pin != NULL) {
    hw_gpio_set_mode(ft6236->int_pin, hw_gpio_pullup);
  }

  if (ft6236->reset_pin != NULL) {
    // Active-low with an internal pull-up (datasheet Figure 3-2) - drive
    // it low ourselves for the reset pulse, then explicitly high to
    // release it (own the line outright rather than relying on the
    // internal pull-up's own rise time). See dev_ft6236_init()'s own doc
    // for where FT6236_RESET_PULSE_MS/_WAIT_MS come from.
    hw_gpio_set_mode(ft6236->reset_pin, hw_gpio_output);
    hw_gpio_set(ft6236->reset_pin, false);
    sys_sleep_ms(FT6236_RESET_PULSE_MS);
    hw_gpio_set(ft6236->reset_pin, true);
    sys_sleep_ms(FT6236_RESET_WAIT_MS);
  }

  uint8_t frame[FT6236_DATA_LENGTH] = {0};
  if (!_dev_ft6236_read_frame(ft6236, frame, sizeof(frame))) {
    dev_ft6236_deinit(ft6236);
    return NULL;
  }

  return ft6236;
}

void dev_ft6236_deinit(dev_ft6236_t *ft6236) {
  if (ft6236 == NULL) {
    return;
  }
  sys_free(ft6236);
}

void dev_ft6236_set_callback(dev_ft6236_t *ft6236,
                             dev_ft6236_callback_t callback, void *userdata) {
  if (ft6236 == NULL) {
    return;
  }
  ft6236->callback = callback;
  ft6236->userdata = userdata;
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

bool dev_ft6236_irq_active(const dev_ft6236_t *ft6236) {
  if (ft6236 == NULL || ft6236->int_pin == NULL) {
    return false;
  }
  bool level = hw_gpio_get(ft6236->int_pin);
  return ft6236->irq_active_low ? !level : level;
}

bool dev_ft6236_has_interrupt_pin(const dev_ft6236_t *ft6236) {
  return ft6236 != NULL && ft6236->int_pin != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

void dev_ft6236_poll(dev_ft6236_t *ft6236) {
  if (ft6236 == NULL) {
    return;
  }

  uint64_t now = sys_timestamp_ms();
  // INT is a per-frame pulse (see dev_ft6236_register_hid()'s own doc),
  // not a level held for the duration of a touch - a poll landing between
  // two pulses reads dev_ft6236_irq_active() as false even though a touch
  // frame was ready moments ago and may be again moments from now, with
  // no guarantee some later poll's own timing ever happens to line up
  // with a pulse. Forcing a real read at least every
  // FT6236_IRQ_RECONCILE_MS bounds how long that can ever go on for,
  // rather than leaving a first touch (had_touch still false, so nothing
  // else here forces a read either) possibly undetected indefinitely.
  bool reconcile_due =
      ft6236->last_read_ms == 0 ||
      (now - ft6236->last_read_ms) >= FT6236_IRQ_RECONCILE_MS;

  if (ft6236->int_pin != NULL && !dev_ft6236_irq_active(ft6236) &&
      !ft6236->had_touch && !reconcile_due) {
    return;
  }

  uint8_t frame[FT6236_DATA_LENGTH] = {0};
  if (!_dev_ft6236_read_frame(ft6236, frame, sizeof(frame))) {
    return;
  }
  ft6236->last_read_ms = now;

  hid_touch_t touches[DEV_FT6236_MAX_POINTS];
  uint8_t touch_count = 0;
  _dev_ft6236_parse_frame(ft6236, frame, touches, &touch_count);
  ft6236->had_touch = touch_count > 0u;

  // FT6236 has no FIFO/"new sample" flag of its own (unlike STMPE610 -
  // see dev_ft6236_poll()'s own doc) - compare each slot against its
  // previous state ourselves and only fire for a genuine change. Struct
  // memcmp is safe here because every write to a hid_touch_t
  // (_dev_ft6236_clear_touches()/_dev_ft6236_parse_frame(), both used
  // above) memset()s first - see last_touches's own doc in ft6236.h.
  for (uint8_t i = 0; i < DEV_FT6236_MAX_POINTS; i++) {
    if (memcmp(&touches[i], &ft6236->last_touches[i], sizeof(touches[i])) !=
        0) {
      // A repeat is never delivered without a preceding on for this slot
      // - synthesize the on first if the last state we actually
      // delivered was off (e.g. a missed/dropped down, or one poll's own
      // idle read landing between the real down and the first move).
      // Consumers can then always treat "repeat" as "this slot is
      // already known to be down", never a state they have to infer.
      bool was_off = ft6236->last_touches[i].state == hid_state_off;
      hid_touch_t touch = touches[i];
      ft6236->last_touches[i] = touch;
      if (ft6236->callback != NULL) {
        if (was_off && touch.state == hid_state_repeat) {
          hid_touch_t synthetic_on = touch;
          synthetic_on.state = hid_state_on;
          ft6236->callback(ft6236, &synthetic_on, ft6236->userdata);
        }
        ft6236->callback(ft6236, &touch, ft6236->userdata);
      }
    }
  }
}
