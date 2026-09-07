#include <picofuse/app.h>
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>

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
  case hid_event_type_keycode:
    if (hid_event->data.keycode.keycode == KEYCODE_BUTTON_USER &&
        (hid_event->data.keycode.state & hid_state_on) != 0) {
      sys_debugf("presto", "on_event: user button pressed, shutting down");
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
