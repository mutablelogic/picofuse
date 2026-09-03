#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

static const char *_touch_event_name(dev_ft6236_touch_event_t event) {
  switch (event) {
  case dev_ft6236_touch_down:
    return "down";
  case dev_ft6236_touch_move:
    return "move";
  case dev_ft6236_touch_up:
  default:
    return "up";
  }
}

test_main_hw(0) {
  if (hw_i2c_count() == 0) {
    sys_printf("[dev_ft6236] no I2C adapter available\n");
    return;
  }

  hw_deviceio_t *device = hw_i2c_init_default(DEV_FT6236_I2C_ADDR_DEFAULT);
  if (device == NULL) {
    sys_printf("[dev_ft6236] hw_i2c_init_default() failed\n");
    return;
  }
  if (!hw_i2c_detect(device, DEV_FT6236_I2C_ADDR_DEFAULT)) {
    sys_printf("[dev_ft6236] no FT6236 detected on I2C\n");
    hw_deviceio_deinit(device);
    return;
  }

  // No interrupt pin - dev_ft6236_poll() falls back to always issuing the
  // I2C read rather than gating on dev_ft6236_irq_active().
  dev_ft6236_t *ft6236 = dev_ft6236_init(device, NULL, NULL);
  if (ft6236 == NULL) {
    sys_printf("[dev_ft6236] dev_ft6236_init() failed\n");
    hw_deviceio_deinit(device);
    return;
  }

  test_assert(dev_ft6236_irq_active(ft6236) == false); // no int_pin configured

  // A few polls in a row. There's no guarantee anything is touching the
  // panel during a test run, so this only checks the call succeeds and
  // reports plausible values - not that a touch is actually present.
  for (int i = 0; i < 5; i++) {
    dev_ft6236_touch_t touches[DEV_FT6236_MAX_POINTS] = {0};
    uint8_t touch_count = 0;
    test_assert(dev_ft6236_poll(ft6236, touches, &touch_count));
    test_assert(touch_count <= DEV_FT6236_MAX_POINTS);

    sys_printf("[dev_ft6236] [%d] touch_count=%u\n", i, touch_count);
    for (uint8_t slot = 0; slot < DEV_FT6236_MAX_POINTS; slot++) {
      sys_printf("[dev_ft6236]   slot=%u event=%s id=%u x=%u y=%u\n", slot,
                 _touch_event_name(touches[slot].event), touches[slot].id,
                 touches[slot].x, touches[slot].y);
    }

    sys_sleep_ms(200);
  }

  dev_ft6236_deinit(ft6236);
  hw_deviceio_deinit(device);
}
