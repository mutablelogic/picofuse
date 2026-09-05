#include "stmpe610.h"
#include <picofuse/dev/stmpe610.h>
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// A register read is 3 bytes under one chip-select assertion: the address
// (read bit set), a dummy turnaround byte, then the real data byte -
// matching Adafruit_STMPE610's own readRegister8() exactly (this chip's
// registers, unlike the FIFO data register, are not auto-incrementing, so
// a multi-byte read has to repeat this per byte, not burst them in one
// transfer - see stmpe610.h's register comment on how this was verified).
static bool _dev_stmpe610_read_reg8(dev_stmpe610_t *stmpe610, uint8_t reg,
                                    uint8_t *out) {
  uint8_t buf[3] = {(uint8_t)(reg | STMPE610_READ_BIT), 0x00, 0x00};
  if (hw_deviceio_xfr(stmpe610->device, buf, 2, 1, 0u) != 3) {
    return false;
  }
  *out = buf[2];
  return true;
}

static bool _dev_stmpe610_write_reg(dev_stmpe610_t *stmpe610, uint8_t reg,
                                    uint8_t value) {
  return hw_deviceio_write_reg(stmpe610->device, reg, &value, 1, 0u) == 1;
}

static bool _dev_stmpe610_probe(dev_stmpe610_t *stmpe610) {
  uint8_t hi = 0, lo = 0;
  if (!_dev_stmpe610_read_reg8(stmpe610, STMPE610_REG_CHIP_ID, &hi) ||
      !_dev_stmpe610_read_reg8(stmpe610, STMPE610_REG_CHIP_ID + 1, &lo)) {
    return false;
  }
  uint16_t chip_id = ((uint16_t)hi << 8) | lo;
  return chip_id == STMPE610_CHIP_ID_VALUE;
}

// Init sequence taken from Adafruit_STMPE610's own begin() (this project
// has no vendored STMPE610 datasheet - see stmpe610.h's register comment).
static bool _dev_stmpe610_configure(dev_stmpe610_t *stmpe610) {
  if (!_dev_stmpe610_write_reg(stmpe610, STMPE610_REG_SYS_CTRL1,
                               STMPE610_SYS_CTRL1_RESET)) {
    return false;
  }
  sys_sleep_ms(10); // Let the soft reset settle before talking again.

  if (!_dev_stmpe610_probe(stmpe610)) {
    return false;
  }

  bool ok = true;
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_SYS_CTRL2, 0x00);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_TSC_CTRL,
                                STMPE610_TSC_CTRL_XYZ | STMPE610_TSC_CTRL_EN);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_INT_EN,
                                STMPE610_INT_EN_TOUCHDET);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_ADC_CTRL1,
                                STMPE610_ADC_CTRL1_10BIT | (0x6u << 4));
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_ADC_CTRL2,
                                STMPE610_ADC_CTRL2_6_5MHZ);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_TSC_CFG,
                                STMPE610_TSC_CFG_4SAMPLE |
                                    STMPE610_TSC_CFG_DELAY_1MS |
                                    STMPE610_TSC_CFG_SETTLE_5MS);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_TSC_FRACTION_Z, 0x06);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_FIFO_TH, 0x01);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_FIFO_STA,
                                STMPE610_FIFO_STA_RESET);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_FIFO_STA, 0x00);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_TSC_I_DRIVE,
                                STMPE610_TSC_I_DRIVE_50MA);
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_INT_STA, 0xFF);
  // INT_POLARITY left clear (active low/falling edge): the datasheet's own
  // pin table documents INT as open-drain, and this board's schematic has
  // no external pull-up on it - only an internal pull-up (see
  // dev_stmpe610_init()) can give it a defined idle level, which only
  // works with active-low (the chip can pull low, never drive high).
  ok &= _dev_stmpe610_write_reg(stmpe610, STMPE610_REG_INT_CTRL,
                                STMPE610_INT_CTRL_ENABLE);
  return ok;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

void dev_stmpe610_default_config(dev_stmpe610_config_t *config) {
  if (config == NULL) {
    return;
  }
  memset(config, 0, sizeof(*config));
  // STMPE610's INT pin is open-drain (datasheet pin table) - it can only
  // pull low, never drive high, so active-low is the only polarity that
  // works with a pull-up (internal or external) giving it a defined idle
  // level. _dev_stmpe610_configure() configures INT_CTRL to match.
  config->irq_active_low = true;
}

dev_stmpe610_t *dev_stmpe610_init(hw_deviceio_t *device, hw_gpio_t *int_pin,
                                  const dev_stmpe610_config_t *config) {
  if (device == NULL) {
    return NULL;
  }

  dev_stmpe610_config_t resolved;
  dev_stmpe610_default_config(&resolved);
  if (config != NULL) {
    resolved = *config;
  }

  dev_stmpe610_t *stmpe610 = sys_calloc(1, sizeof(*stmpe610));
  if (stmpe610 == NULL) {
    return NULL;
  }

  stmpe610->device = device;
  stmpe610->int_pin = int_pin;
  stmpe610->irq_active_low = resolved.irq_active_low;

  if (stmpe610->int_pin != NULL) {
    // Pull-up, not pulldown: INT is open-drain with no external pull-up on
    // this board (see the config comment above), so the pin would float
    // when idle without one.
    hw_gpio_set_mode(stmpe610->int_pin, hw_gpio_pullup);
  }

  if (!_dev_stmpe610_configure(stmpe610)) {
    dev_stmpe610_deinit(stmpe610);
    return NULL;
  }

  return stmpe610;
}

void dev_stmpe610_deinit(dev_stmpe610_t *stmpe610) {
  if (stmpe610 == NULL) {
    return;
  }
  sys_free(stmpe610);
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

bool dev_stmpe610_irq_active(const dev_stmpe610_t *stmpe610) {
  if (stmpe610 == NULL || stmpe610->int_pin == NULL) {
    return false;
  }
  bool level = hw_gpio_get(stmpe610->int_pin);
  return stmpe610->irq_active_low ? !level : level;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool dev_stmpe610_poll(dev_stmpe610_t *stmpe610, dev_stmpe610_touch_t *touch) {
  if (stmpe610 == NULL || touch == NULL) {
    return false;
  }

  if (stmpe610->int_pin != NULL && !dev_stmpe610_irq_active(stmpe610) &&
      !stmpe610->had_touch) {
    memset(touch, 0, sizeof(*touch));
    touch->event = dev_stmpe610_touch_up;
    return true;
  }

  uint8_t fifo_sta = 0;
  if (!_dev_stmpe610_read_reg8(stmpe610, STMPE610_REG_FIFO_STA, &fifo_sta)) {
    return false;
  }

  if (!(fifo_sta & STMPE610_FIFO_STA_EMPTY)) {
    // The FIFO data register is the one register on this chip that *is*
    // safe to pop repeatedly by address alone - each read of the same
    // address returns the next queued byte (see stmpe610.h).
    uint8_t data[STMPE610_TSC_DATA_LEN];
    for (size_t i = 0; i < sizeof(data); i++) {
      if (!_dev_stmpe610_read_reg8(stmpe610, STMPE610_REG_TSC_DATA_NONINC,
                                   &data[i])) {
        return false;
      }
    }
    touch->x = ((uint16_t)data[0] << 4) | (data[1] >> 4);
    touch->y = (((uint16_t)data[1] & 0x0Fu) << 8) | data[2];
    touch->z = data[3];
    touch->event =
        stmpe610->had_touch ? dev_stmpe610_touch_move : dev_stmpe610_touch_down;
    stmpe610->had_touch = true;
    stmpe610->last_x = touch->x;
    stmpe610->last_y = touch->y;
    stmpe610->last_z = touch->z;
    return true;
  }

  uint8_t tsc_ctrl = 0;
  if (!_dev_stmpe610_read_reg8(stmpe610, STMPE610_REG_TSC_CTRL, &tsc_ctrl)) {
    return false;
  }

  if (!(tsc_ctrl & STMPE610_TSC_CTRL_TOUCHING) && stmpe610->had_touch) {
    stmpe610->had_touch = false;
    touch->event = dev_stmpe610_touch_up;
    touch->x = stmpe610->last_x;
    touch->y = stmpe610->last_y;
    touch->z = 0;
    return true;
  }

  // Still touching (or still idle) with nothing new in the FIFO - report
  // the last known sample again rather than a spurious change.
  touch->event =
      stmpe610->had_touch ? dev_stmpe610_touch_move : dev_stmpe610_touch_up;
  touch->x = stmpe610->last_x;
  touch->y = stmpe610->last_y;
  touch->z = stmpe610->had_touch ? stmpe610->last_z : 0;
  return true;
}
