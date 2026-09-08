#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// Needed for the PIMORONI_PRESTO_TOUCH_* board macros checked below -
// picofuse/hw.h is platform-agnostic and pulls in none of the Pico SDK's
// own headers, so without this every #if defined() here silently sees an
// undefined macro regardless of what the board actually provides (see
// hw/pico/led_default.c's own doc on the same issue).
#include <pico.h>

// Presto's own touch panel over I2C - detection only (no human touch
// required to pass), plus a short window that prints anything a real touch
// produces if one happens to land during the run.
#if defined(PIMORONI_PRESTO_TOUCH_I2C) &&                                     \
    defined(PIMORONI_PRESTO_TOUCH_SDA_PIN) &&                                \
    defined(PIMORONI_PRESTO_TOUCH_SCL_PIN)

static void _on_touch(dev_ft6236_t *ft6236, const hid_touch_t *touch,
                      void *userdata) {
  (void)ft6236;
  (void)userdata;
  sys_printf("[dev_ft6236] touch slot=%u state=%u x=%d y=%d\n", touch->slot,
             touch->state, touch->point.x, touch->point.y);
}

test_main_hw(0) {
  hw_gpio_t *sda =
      hw_gpio_init(0, PIMORONI_PRESTO_TOUCH_SDA_PIN, hw_gpio_i2c);
  hw_gpio_t *scl =
      hw_gpio_init(0, PIMORONI_PRESTO_TOUCH_SCL_PIN, hw_gpio_i2c);
  test_assert(sda != NULL);
  test_assert(scl != NULL);

  // Board header (include/boards/presto.h) declares
  // PIMORONI_PRESTO_TOUCH_I2C_ADDR as 0x48, but dev_ft6236.h documents
  // 0x38 as the only address a real FT6236 ever responds at (no
  // address-select pin) - probe both on the real bus rather than assume
  // either is right for whatever chip is actually wired up here.
  hw_deviceio_t *probe = hw_i2c_init(PIMORONI_PRESTO_TOUCH_I2C,
                                     DEV_FT6236_I2C_ADDR_DEFAULT, sda, scl);
  test_assert(probe != NULL);

  bool ack_default = hw_i2c_detect(probe, DEV_FT6236_I2C_ADDR_DEFAULT);
#if defined(PIMORONI_PRESTO_TOUCH_I2C_ADDR)
  bool ack_board = hw_i2c_detect(probe, PIMORONI_PRESTO_TOUCH_I2C_ADDR);
#else
  bool ack_board = false;
#endif
  sys_printf("[dev_ft6236] I2C%u ack: 0x%02X(FT6236 default)=%d",
             PIMORONI_PRESTO_TOUCH_I2C, DEV_FT6236_I2C_ADDR_DEFAULT,
             ack_default);
#if defined(PIMORONI_PRESTO_TOUCH_I2C_ADDR)
  sys_printf(" 0x%02X(board)=%d", PIMORONI_PRESTO_TOUCH_I2C_ADDR, ack_board);
#endif
  sys_printf("\n");

  hw_deviceio_deinit(probe);

  uint8_t addr = ack_board
#if defined(PIMORONI_PRESTO_TOUCH_I2C_ADDR)
                     ? PIMORONI_PRESTO_TOUCH_I2C_ADDR
#else
                     ? DEV_FT6236_I2C_ADDR_DEFAULT
#endif
                     : DEV_FT6236_I2C_ADDR_DEFAULT;

  hw_deviceio_t *device =
      hw_i2c_init(PIMORONI_PRESTO_TOUCH_I2C, addr, sda, scl);
  test_assert(device != NULL);

  hw_gpio_t *int_pin = NULL;
#if defined(PIMORONI_PRESTO_TOUCH_INT_PIN)
  int_pin = hw_gpio_init(0, PIMORONI_PRESTO_TOUCH_INT_PIN, hw_gpio_pullup);
#endif

  // No PIMORONI_PRESTO_TOUCH_*RESET*_PIN macro exists on this board's own
  // header - the reset_pin config field exists for wiring that does
  // expose one, not exercised here.
  dev_ft6236_config_t config;
  dev_ft6236_default_config(&config);
  config.int_pin = int_pin;

  dev_ft6236_t *ft6236 = dev_ft6236_init(device, &config);
  sys_printf("[dev_ft6236] detect at 0x%02X: %s\n", addr,
             ft6236 != NULL ? "found" : "not found");
  test_assert(ft6236 != NULL);

  dev_ft6236_set_callback(ft6236, _on_touch, NULL);

  // Informational only - poll for a couple of seconds; _on_touch() prints
  // whatever shows up if the screen happens to be touched during the run.
  // Not asserted: nothing here can require a human touch to pass.
  for (int i = 0; i < 20; i++) {
    dev_ft6236_poll(ft6236);
    sys_sleep_ms(100);
  }

  dev_ft6236_deinit(ft6236);
  hw_deviceio_deinit(device);
  if (int_pin != NULL) {
    hw_gpio_deinit(int_pin);
  }
  hw_gpio_deinit(sda);
  hw_gpio_deinit(scl);
}

#else

test_main_hw(0) {
  sys_printf("[dev_ft6236] board has no PIMORONI_PRESTO_TOUCH_* pins - "
             "nothing to exercise\n");
}

#endif
