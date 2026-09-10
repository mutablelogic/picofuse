#include <picofuse/app.h>
#include <picofuse/dev.h>
#include <picofuse/hid.h>
#include <picofuse/pix.h>
#include <picofuse/sys.h>

static pix_display_t *_display = NULL;

// How often this display should be flushed, at most - see dev_sdl_init()'s
// own doc on interval_ms. 1000ms (1fps) so "due a redraw" in the debug log
// is visibly throttled rather than firing on every poll.
#define SDL_EXAMPLE_INTERVAL_MS 1000u

// Called once per redraw with a locked, ready-to-draw-into bitmap - see
// pix_display_draw_t's own doc. The fill color rotates on every call (three
// phase-shifted sawtooths, one per channel), purely so successive redraws
// are visibly distinguishable.
static void _on_draw(pix_display_t *display, pix_bitmap_t *bitmap,
                     void *userdata) {
  (void)display;
  (void)userdata;

  static uint8_t hue = 0;
  pix_color_t color = PIX_COLOR_RGB(hue, (uint8_t)(hue + 85), (uint8_t)(hue + 170));
  hue += 32;

  pix_bitmap_fill_rect(bitmap, (pix_point_t){0}, bitmap->size, color);

  sys_printf("Draw callback: cleared %ux%u bitmap\n", bitmap->size.w,
            bitmap->size.h);
}

static void _on_start(app_t *app, void *userdata) {
  (void)app;
  (void)userdata;

  _display =
      dev_sdl_init("picofuse", (pix_size_t){.w = 320, .h = 240},
                   PIX_FMT_RGB565, dev_sdl_none, SDL_EXAMPLE_INTERVAL_MS);
  if (_display != NULL) {
    sys_printf("SDL display created\n");
    pix_display_set_callback(_display, _on_draw, NULL);
  } else {
    sys_printf("SDL display not created (backend not available yet)\n");
  }
}

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  (void)app;
  (void)userdata;

  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event == NULL) {
    return;
  }

  // Ctrl-C/SIGTERM on a host build - a Pico board has no such signals, so
  // this branch never fires there; app_shutdown() is only reachable by
  // physically resetting the board instead.
  if (hid_event->type == hid_event_type_signal) {
    sys_printf("Signal event: %u, shutting down\n",
              (unsigned)hid_event->data.signal.signal);
    pix_display_deinit(_display);
    _display = NULL;
    app_shutdown(0);
  }

  hid_event_free(hid_event);
}

int main(int argc, char *argv[]) {
  return app_main(argc, argv, app_flag_stdio_rtt | app_flag_signal, _on_start,
                  _on_event, NULL);
}
