#include "../led/led.h"
#include <picofuse/pix/color.h>
#include <picofuse/sys.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "led_neopixel.pio.h"
#include "pico/time.h"

#define LED_NEOPIXEL_FREQ_HZ 800000.0f // WS2812 bit rate

// How long to wait for TX FIFO space before giving up on a flush - PIO
// output should never actually stall this long, but pio_sm_put_blocking()
// spinning forever with no way out is exactly the class of bug that once
// bit sys_puts() (see sys/pico/puts.c) when a backend's buffer never
// drained: better to fail the write than hang the whole device.
#define LED_NEOPIXEL_FIFO_TIMEOUT_US 3000u

// WS2812's data line must be held low for this long after the last bit
// before the chain actually latches the new pixel values.
#define LED_NEOPIXEL_LATCH_US 80u

///////////////////////////////////////////////////////////////////////////////
// TYPES

// pixels stores one pix_color_t per LED, heap-allocated since led_count is
// only known at init time and won't generally fit in HW_LED_CONTEXT_SIZE
// for a long chain.
typedef struct {
  PIO pio;
  uint sm;
  uint offset;
  uint pin;
  pix_color_t *pixels;
  uint8_t led_count;
} _hw_led_neopixel_ctx_t;

_Static_assert(sizeof(_hw_led_neopixel_ctx_t) <= HW_LED_CONTEXT_SIZE,
              "_hw_led_neopixel_ctx_t exceeds HW_LED_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// pix_color_t is 0xRRGGBBAA - WS2812 wants each pixel on the wire as GRB,
// alpha unused.
static inline uint32_t _hw_led_neopixel_pack_color(pix_color_t color) {
  return ((uint32_t)pix_color_g(color) << 16) |
        ((uint32_t)pix_color_r(color) << 8) | pix_color_b(color);
}

// Shifts the whole chain's current pixel buffer out over PIO - every
// hw_led_set()/clear() re-sends the entire chain, since WS2812 has no way
// to address a single LED independently of the ones before it in the
// chain. Bounded by LED_NEOPIXEL_FIFO_TIMEOUT_US per word (see its
// comment) and finished off with the latch delay every WS2812 update
// needs before it takes visible effect.
static bool _hw_led_neopixel_flush(const _hw_led_neopixel_ctx_t *ctx) {
  for (uint8_t i = 0; i < ctx->led_count; i++) {
    uint32_t word = _hw_led_neopixel_pack_color(ctx->pixels[i]);

    uint64_t start = time_us_64();
    while (pio_sm_is_tx_fifo_full(ctx->pio, ctx->sm)) {
      if (time_us_64() - start > LED_NEOPIXEL_FIFO_TIMEOUT_US) {
        return false;
      }
    }
    pio_sm_put(ctx->pio, ctx->sm, word << 8u);
  }

  sleep_us(LED_NEOPIXEL_LATCH_US);
  return true;
}

// A simple on/off hw_led_t doesn't expose per-pixel color, so turning an
// index on lights it plain white the first time - but if it already holds
// some other color (set some other way, once this driver grows a way to
// do that), re-enabling it preserves that color instead of clobbering it
// back to white. A NeoPixel chain wanting real color control from the
// start should be driven directly rather than through this interface.
static bool _hw_led_neopixel_set(hw_led_t *led, uint8_t index, bool enabled) {
  _hw_led_neopixel_ctx_t *ctx = _hw_led_context(led);
  if (index >= ctx->led_count) {
    return false;
  }

  if (!enabled) {
    ctx->pixels[index] = 0;
  } else if (ctx->pixels[index] == 0) {
    ctx->pixels[index] = PIX_COLOR_WHITE;
  }

  return _hw_led_neopixel_flush(ctx);
}

// Unlike _set(), which only ever touches one index, hw_led_clear() turns
// off every LED in the chain (see the public API doc).
static bool _hw_led_neopixel_clear(hw_led_t *led) {
  _hw_led_neopixel_ctx_t *ctx = _hw_led_context(led);
  memset(ctx->pixels, 0, (size_t)ctx->led_count * sizeof(pix_color_t));
  return _hw_led_neopixel_flush(ctx);
}

static void _hw_led_neopixel_deinit(hw_led_t *led) {
  _hw_led_neopixel_ctx_t *ctx = _hw_led_context(led);
  pio_sm_set_enabled(ctx->pio, ctx->sm, false);
  pio_remove_program_and_unclaim_sm(&led_neopixel_program, ctx->pio, ctx->sm,
                                    ctx->offset);
  sys_free(ctx->pixels);
}

static const hw_led_ops_t _hw_led_neopixel_ops = {
    .set = _hw_led_neopixel_set,
    .clear = _hw_led_neopixel_clear,
    .deinit = _hw_led_neopixel_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_led_t *hw_led_init_neopixel(hw_gpio_t *gpio, uint8_t led_count) {
  if (gpio == NULL || led_count == 0) {
    return NULL;
  }

  pix_color_t *pixels = sys_calloc(led_count, sizeof(pix_color_t));
  if (pixels == NULL) {
    return NULL;
  }

  uint pin = hw_gpio_pin(gpio);
  PIO pio;
  uint sm;
  uint offset;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(
          &led_neopixel_program, &pio, &sm, &offset, pin, 1, true)) {
    sys_free(pixels);
    return NULL;
  }

  led_neopixel_program_init(pio, sm, offset, pin, LED_NEOPIXEL_FREQ_HZ);

  hw_led_t *led = _hw_led_alloc(&_hw_led_neopixel_ops);
  if (led == NULL) {
    pio_sm_set_enabled(pio, sm, false);
    pio_remove_program_and_unclaim_sm(&led_neopixel_program, pio, sm, offset);
    sys_free(pixels);
    return NULL;
  }

  _hw_led_neopixel_ctx_t *ctx = _hw_led_context(led);
  ctx->pio = pio;
  ctx->sm = sm;
  ctx->offset = offset;
  ctx->pin = pin;
  ctx->pixels = pixels;
  ctx->led_count = led_count;

  _hw_led_neopixel_flush(ctx); // chain starts all-off (pixels is zeroed)
  return led;
}
