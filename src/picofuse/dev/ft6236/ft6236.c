#include "ft6236.h"
#include <picofuse/dev/ft6236.h>
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static void
_dev_ft6236_clear_touches(dev_ft6236_touch_t touches[DEV_FT6236_MAX_POINTS],
                          uint8_t *out_touch_count) {
  if (touches != NULL) {
    memset(touches, 0, sizeof(dev_ft6236_touch_t) * DEV_FT6236_MAX_POINTS);
    for (size_t i = 0; i < DEV_FT6236_MAX_POINTS; i++) {
      touches[i].event = dev_ft6236_touch_up;
      touches[i].id = (uint8_t)i;
    }
  }
  if (out_touch_count != NULL) {
    *out_touch_count = 0u;
  }
}

static dev_ft6236_touch_event_t _dev_ft6236_map_event(uint8_t event_code) {
  switch (event_code) {
  case FT6236_EVENT_DOWN:
    return dev_ft6236_touch_down;
  case FT6236_EVENT_CONTACT:
    return dev_ft6236_touch_move;
  case FT6236_EVENT_UP:
  case FT6236_EVENT_NONE:
  default:
    return dev_ft6236_touch_up;
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

static void
_dev_ft6236_parse_frame(const uint8_t *buffer,
                        dev_ft6236_touch_t touches[DEV_FT6236_MAX_POINTS],
                        uint8_t *out_touch_count) {
  _dev_ft6236_clear_touches(touches, out_touch_count);
  if (buffer == NULL || touches == NULL) {
    return;
  }

  uint8_t count = buffer[2] & FT6236_TOUCH_COUNT_MASK;
  if (count > DEV_FT6236_MAX_POINTS) {
    count = DEV_FT6236_MAX_POINTS;
  }

  for (uint8_t index = 0u; index < count; index++) {
    const uint8_t *point = &buffer[3u + ((size_t)index * 6u)];
    uint8_t touch_id =
        (point[2] >> FT6236_TOUCH_ID_SHIFT) & FT6236_TOUCH_ID_MASK;
    uint8_t slot = touch_id < DEV_FT6236_MAX_POINTS ? touch_id : index;

    dev_ft6236_touch_event_t event = _dev_ft6236_map_event(
        (point[0] >> FT6236_TOUCH_EVENT_SHIFT) & FT6236_TOUCH_EVENT_MASK);

    touches[slot].event = event;
    touches[slot].id = touch_id;
    touches[slot].x =
        ((uint16_t)(point[0] & FT6236_TOUCH_POS_MASK) << 8) | point[1];
    touches[slot].y =
        ((uint16_t)(point[2] & FT6236_TOUCH_POS_MASK) << 8) | point[3];
    if (event != dev_ft6236_touch_up && out_touch_count != NULL) {
      (*out_touch_count)++;
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
}

dev_ft6236_t *dev_ft6236_init(hw_deviceio_t *device, hw_gpio_t *int_pin,
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
  ft6236->int_pin = int_pin;
  ft6236->irq_active_low = resolved.irq_active_low;

  if (ft6236->int_pin != NULL) {
    hw_gpio_set_mode(ft6236->int_pin, hw_gpio_pullup);
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

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

bool dev_ft6236_irq_active(const dev_ft6236_t *ft6236) {
  if (ft6236 == NULL || ft6236->int_pin == NULL) {
    return false;
  }
  bool level = hw_gpio_get(ft6236->int_pin);
  return ft6236->irq_active_low ? !level : level;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool dev_ft6236_poll(dev_ft6236_t *ft6236,
                     dev_ft6236_touch_t touches[DEV_FT6236_MAX_POINTS],
                     uint8_t *out_touch_count) {
  if (ft6236 == NULL || touches == NULL) {
    return false;
  }

  if (ft6236->int_pin != NULL && !dev_ft6236_irq_active(ft6236) &&
      !ft6236->had_touch) {
    _dev_ft6236_clear_touches(touches, out_touch_count);
    return true;
  }

  uint8_t frame[FT6236_DATA_LENGTH] = {0};
  if (!_dev_ft6236_read_frame(ft6236, frame, sizeof(frame))) {
    return false;
  }

  uint8_t touch_count = 0;
  _dev_ft6236_parse_frame(frame, touches, &touch_count);
  if (out_touch_count != NULL) {
    *out_touch_count = touch_count;
  }
  ft6236->had_touch = touch_count > 0u;
  return true;
}
