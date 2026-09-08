#include <picofuse/app.h>
#include <picofuse/dev.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

// Needed for the PIMORONI_PRESTO_TOUCH_* board macros checked below -
// picofuse/hw.h is platform-agnostic and pulls in none of the Pico SDK's
// own headers, so without this every #if defined() here silently sees an
// undefined macro regardless of what the board actually provides (see
// hw/pico/led_default.c's own doc on the same issue). Guarded, unlike
// that Pico-only file: this example is also built for host platforms
// (see the root CMakeLists.txt's unconditional add_subdirectory), where
// <pico.h> doesn't exist at all - SYSTEM_NAME_PICO is the same
// compile-time platform switch sys/event/runloop.c's own #ifdef
// SYSTEM_NAME_PICO uses.
#ifdef SYSTEM_NAME_PICO
#include <pico.h>
#endif

// Pimoroni Presto (PICO_BOARD=presto): 7 NeoPixels on one WS2812 chain -
// app_flag_led's own hw_led_init_default() already detects that via the
// board header's PICO_DEFAULT_WS2812_PIN/_NUM_PIXELS, so this reaches it
// the same way any other board's single on-board LED would (see
// hw/led.h's own doc). Alternates between two chase animations - one lit
// pixel circling among dark ones, then one dark pixel circling among lit
// ones - cycling the lit color through red/green/blue/white each time a
// circle completes, until the board's own user button is pressed.
#define PRESTO_ANIMATION_PERIOD_MS 150u
#define PRESTO_PIXEL_COUNT_DEFAULT 7u

typedef enum {
  presto_phase_chase_lit,  ///< One pixel on, the rest off.
  presto_phase_chase_dark, ///< One pixel off, the rest on.
} presto_phase_t;

static const pix_color_t _colors[] = {
    PIX_COLOR_RED,
    PIX_COLOR_GREEN,
    PIX_COLOR_BLUE,
    PIX_COLOR_WHITE,
};
#define PRESTO_COLOR_COUNT (sizeof(_colors) / sizeof(_colors[0]))

static uint8_t _pixel_count = PRESTO_PIXEL_COUNT_DEFAULT;
static presto_phase_t _phase = presto_phase_chase_lit;
static uint8_t _position = 0;
static uint8_t _color_index = 0;

// Presto's own touch panel over I2C - registered with HID (see
// _touch_start()/_touch_stop() below) so touches arrive as ordinary
// hid_event_type_touch events in _on_event(), the same as the timer/
// button events already handled there.
#if defined(PIMORONI_PRESTO_TOUCH_I2C) &&                                      \
    defined(PIMORONI_PRESTO_TOUCH_SDA_PIN) &&                                  \
    defined(PIMORONI_PRESTO_TOUCH_SCL_PIN)
static hw_gpio_t *_touch_sda = NULL;
static hw_gpio_t *_touch_scl = NULL;
static hw_gpio_t *_touch_int = NULL;
static hw_deviceio_t *_touch_device = NULL;
static dev_ft6236_t *_touch_ft6236 = NULL;

static void _touch_start(app_t *app) {
  _touch_sda = hw_gpio_init(0, PIMORONI_PRESTO_TOUCH_SDA_PIN, hw_gpio_i2c);
  _touch_scl = hw_gpio_init(0, PIMORONI_PRESTO_TOUCH_SCL_PIN, hw_gpio_i2c);
  if (_touch_sda == NULL || _touch_scl == NULL) {
    sys_debugf("presto", "touch_start: failed to claim SDA/SCL pins");
    return;
  }

  // Board header (include/boards/presto.h) declares
  // PIMORONI_PRESTO_TOUCH_I2C_ADDR as 0x48 - the real chip on this board
  // acks there, not at dev_ft6236.h's own documented FT6236 default of
  // 0x38 (confirmed on real hardware; presumably a register-compatible
  // variant with a different fixed address).
#if defined(PIMORONI_PRESTO_TOUCH_I2C_ADDR)
  uint8_t addr = PIMORONI_PRESTO_TOUCH_I2C_ADDR;
#else
  uint8_t addr = DEV_FT6236_I2C_ADDR_DEFAULT;
#endif
  _touch_device =
      hw_i2c_init(PIMORONI_PRESTO_TOUCH_I2C, addr, _touch_sda, _touch_scl);
  if (_touch_device == NULL) {
    sys_debugf("presto", "touch_start: hw_i2c_init failed");
    return;
  }

#if defined(PIMORONI_PRESTO_TOUCH_INT_PIN)
  _touch_int = hw_gpio_init(0, PIMORONI_PRESTO_TOUCH_INT_PIN, hw_gpio_pullup);
#endif

  dev_ft6236_config_t config;
  dev_ft6236_default_config(&config);
  config.int_pin = _touch_int;

  _touch_ft6236 = dev_ft6236_init(_touch_device, &config);
  if (_touch_ft6236 == NULL) {
    sys_debugf("presto", "touch_start: dev_ft6236_init failed");
    return;
  }

  if (dev_ft6236_register_hid(app_hid(app), _touch_ft6236, 0, NULL) == NULL) {
    sys_debugf("presto", "touch_start: dev_ft6236_register_hid failed");
  }
}

static void _touch_stop(void) {
  dev_ft6236_deinit(_touch_ft6236);
  _touch_ft6236 = NULL;
  if (_touch_device != NULL) {
    hw_deviceio_deinit(_touch_device);
    _touch_device = NULL;
  }
  if (_touch_int != NULL) {
    hw_gpio_deinit(_touch_int);
    _touch_int = NULL;
  }
  if (_touch_sda != NULL) {
    hw_gpio_deinit(_touch_sda);
    _touch_sda = NULL;
  }
  if (_touch_scl != NULL) {
    hw_gpio_deinit(_touch_scl);
    _touch_scl = NULL;
  }
}
#else
static void _touch_start(app_t *app) {
  (void)app;
  sys_debugf("presto", "touch_start: board has no PIMORONI_PRESTO_TOUCH_* "
                       "pins - nothing to exercise");
}
static void _touch_stop(void) {}
#endif

static void _draw(hw_led_t *led) {
  pix_color_t color = _colors[_color_index];
  for (uint8_t i = 0; i < _pixel_count; i++) {
    bool lit = (_phase == presto_phase_chase_lit) ? (i == _position)
                                                  : (i != _position);
    hw_led_set_color(led, i, lit ? color : PIX_COLOR_BLACK);
  }
}

static void _stop(app_t *app) {
  hw_led_t *led = app_led(app);
  if (led != NULL) {
    hw_led_clear(led);
  }
  _touch_stop();
  app_shutdown(0);
}

static void _on_start(app_t *app, void *userdata) {
  (void)userdata;

  hw_led_t *led = app_led(app);
  if (led == NULL) {
    sys_debugf("presto", "on_start: no default board LED available");
    return;
  }

  uint8_t count = 0;
  hw_led_gpio_default(NULL, &count);
  if (count > 0) {
    _pixel_count = count;
  }

  hw_led_clear(led);
  _draw(led);

  if (hid_register_timer(app_hid(app), 0, PRESTO_ANIMATION_PERIOD_MS, true,
                         NULL) == NULL) {
    sys_debugf("presto", "on_start: hid_register_timer failed");
  }

  _touch_start(app);
}

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  (void)userdata;

  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event == NULL) {
    return;
  }

  switch (hid_event->type) {
  case hid_event_type_timer: {
    _position++;
    if (_position >= _pixel_count) {
      _position = 0;
      _phase = (_phase == presto_phase_chase_lit) ? presto_phase_chase_dark
                                                  : presto_phase_chase_lit;
      _color_index = (_color_index + 1) % PRESTO_COLOR_COUNT;
    }
    hw_led_t *led = app_led(app);
    if (led != NULL) {
      _draw(led);
    }
    break;
  }
  case hid_event_type_touch: {
    char state[32];
    hid_state_to_string(hid_event->data.touch.state, state, sizeof(state));
    sys_printf("[presto] on_event: touch slot=%u state=%s x=%d y=%d\n",
               hid_event->data.touch.slot, state, hid_event->data.touch.point.x,
               hid_event->data.touch.point.y);
    break;
  }
  case hid_event_type_keycode:
    if (hid_event->data.keycode.keycode == KEYCODE_BUTTON_USER &&
        (hid_event->data.keycode.state & hid_state_on) != 0) {
      sys_printf("[presto] on_event: user button pressed, shutting down\n");
      _stop(app);
    }
    break;
  case hid_event_type_signal:
    // Ctrl-C/SIGTERM on a host build - a Pico board has no such signals,
    // it only ever gets here via the user button case above.
    _stop(app);
    break;
  default:
    break;
  }

  hid_event_free(hid_event);
}

int main(int argc, char *argv[]) {
  return app_main(argc, argv,
                  app_flag_signal | app_flag_led | app_flag_user_button |
                      app_flag_stdio_rtt,
                  _on_start, _on_event, NULL);
}
