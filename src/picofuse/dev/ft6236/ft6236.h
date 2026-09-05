#pragma once
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

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct dev_ft6236_t {
  hw_deviceio_t *device;
  hw_gpio_t *int_pin;
  bool irq_active_low;
  bool had_touch;
};
