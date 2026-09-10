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

// How many random rectangles to scatter on top of the black clear, and the
// range of side lengths (in pixels) each one is drawn from.
#define SDL_EXAMPLE_RECT_COUNT 8u
#define SDL_EXAMPLE_RECT_MIN_SIZE 10u
#define SDL_EXAMPLE_RECT_MAX_SIZE 60u

// Random alpha too, not just RGB - bitmap->op defaults to PIX_BLEND (see
// pix_bitmap_t::op's own doc), so overlapping rects actually blend into
// each other and into the black clear beneath them, rather than each one
// just overwriting whatever was there.
static pix_color_t _random_color(void) {
  return PIX_COLOR_RGBA((uint8_t)sys_random_uint32(),
                        (uint8_t)sys_random_uint32(),
                        (uint8_t)sys_random_uint32(),
                        (uint8_t)sys_random_uint32());
}

// Called once per redraw with a locked, ready-to-draw-into bitmap - see
// pix_display_draw_t's own doc. Clears to black, then scatters a handful of
// randomly placed, sized, colored and semi-transparent rectangles on top -
// pix_bitmap_fill_rect() clips each one to the bitmap's own bounds on its
// own, so a rect can safely hang off any edge.
static void _on_draw(pix_display_t *display, pix_bitmap_t *bitmap,
                     void *userdata) {
  (void)display;
  (void)userdata;

  pix_bitmap_fill_rect(bitmap, (pix_point_t){0}, bitmap->size, PIX_COLOR_BLACK);

  for (unsigned i = 0; i < SDL_EXAMPLE_RECT_COUNT; i++) {
    pix_point_t origin = {
        .x = (int16_t)(sys_random_uint32() % bitmap->size.w),
        .y = (int16_t)(sys_random_uint32() % bitmap->size.h),
    };
    pix_size_t size = {
        .w = (uint16_t)(SDL_EXAMPLE_RECT_MIN_SIZE +
                        sys_random_uint32() % (SDL_EXAMPLE_RECT_MAX_SIZE -
                                              SDL_EXAMPLE_RECT_MIN_SIZE)),
        .h = (uint16_t)(SDL_EXAMPLE_RECT_MIN_SIZE +
                        sys_random_uint32() % (SDL_EXAMPLE_RECT_MAX_SIZE -
                                              SDL_EXAMPLE_RECT_MIN_SIZE)),
    };
    pix_bitmap_fill_rect(bitmap, origin, size, _random_color());
  }

  sys_printf("Draw callback: %ux%u bitmap, %u rects\n", bitmap->size.w,
            bitmap->size.h, SDL_EXAMPLE_RECT_COUNT);
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
