#include "../../pix/private.h"
#include <picofuse/dev/st7701.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

// Command1 table - always available regardless of which Command2 bank (if
// any) is currently selected.
#define ST7701_SWRESET 0x01u
#define ST7701_SLPOUT 0x11u
#define ST7701_INVON 0x21u
#define ST7701_DISPON 0x29u
#define ST7701_MADCTL 0x36u
#define ST7701_COLMOD 0x3Au
#define ST7701_CND2BKXSEL                                                      \
  0xFFu // Selects which Command2 bank (if any) is active

// Command2 BK0 register addresses (only meaningful once BK0 is selected).
#define ST7701_BK0_PVGAMCTRL 0xB0u
#define ST7701_BK0_NVGAMCTRL 0xB1u
#define ST7701_BK0_LNESET 0xC0u
#define ST7701_BK0_PORCTRL 0xC1u
#define ST7701_BK0_INVSET 0xC2u
#define ST7701_BK0_RGBCTRL 0xC3u
#define ST7701_BK0_SDIR 0xC7u
#define ST7701_BK0_COLCTRL 0xCDu

// Command2 BK1 register addresses (only meaningful once BK1 is selected).
#define ST7701_BK1_VHRS 0xB0u
#define ST7701_BK1_VCOMS 0xB1u
#define ST7701_BK1_VGHSS 0xB2u
#define ST7701_BK1_TESTCMD 0xB3u
#define ST7701_BK1_VGLS 0xB5u
#define ST7701_BK1_PWCTRL1 0xB7u
#define ST7701_BK1_PWCTRL2 0xB8u
#define ST7701_BK1_PDR1 0xC1u
#define ST7701_BK1_PDR2 0xC2u

// CND2BKxSEL argument bytes - selects/deselects which Command2 bank is
// active. Fixed by the controller, not display-specific.
static const uint8_t _st7701_bkx_disable[] = {0x77, 0x01, 0x00, 0x00, 0x00};
static const uint8_t _st7701_bk0_select[] = {0x77, 0x01, 0x00, 0x00, 0x10};
static const uint8_t _st7701_bk1_select[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static const uint8_t _st7701_bk3_select[] = {0x77, 0x01, 0x00, 0x00, 0x13};

// Longest argument list any single _dev_st7701_command() call below needs.
#define ST7701_MAX_COMMAND_ARGS 16u

// Ma
#define ST7701_LNESET_MAX_HEIGHT 1024u // Line[6:0] is 7 bits: (127+1)*8

///////////////////////////////////////////////////////////////////////////////
// TYPES

// ST7701 context
typedef struct {
  hw_deviceio_t *device;
  hw_pwm_t *bl_pwm; // Optional - NULL if bl_pin wasn't given, or its PWM failed
                    // to init
} _dev_st7701_ctx_t;

_Static_assert(sizeof(_dev_st7701_ctx_t) <= PIX_DISPLAY_CONTEXT_SIZE,
               "_dev_st7701_ctx_t exceeds PIX_DISPLAY_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// OPS DECLARATIONS

// Not yet implemented - see each body's own comment.
static pix_bitmap_t *_dev_st7701_lock(pix_display_t *display);
static void _dev_st7701_unlock(pix_display_t *display);
static bool _dev_st7701_poll(pix_display_t *display);
static void _dev_st7701_deinit(pix_display_t *display);

static const pix_display_ops_t _dev_st7701_ops = {
    .lock = _dev_st7701_lock,
    .unlock = _dev_st7701_unlock,
    .poll = _dev_st7701_poll,
    .deinit = _dev_st7701_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// This panel has no D/CX GPIO - command/data select is packed as the 9th
// bit of every SPI word instead (see dev_st7701_init()'s own doc), which
// is why `device` must be configured with bits_per_word=9 and this passes
// a uint16_t buffer to hw_deviceio_xfr() - see its own doc on word size.
// Sent as a single transfer (one CS assertion) covering the command byte
// and all of its arguments together.
static bool _dev_st7701_command(pix_display_t *display, uint8_t command,
                                const uint8_t *args, size_t len) {
  if (len > ST7701_MAX_COMMAND_ARGS) {
    return false;
  }
  _dev_st7701_ctx_t *ctx = _pix_display_context(display);

  uint16_t words[1 + ST7701_MAX_COMMAND_ARGS];
  words[0] = command; // D/CX = 0 (command)
  for (size_t i = 0; i < len; i++) {
    words[1 + i] = 0x0100u | args[i]; // D/CX = 1 (data)
  }

  size_t total = 1 + len;
  return hw_deviceio_xfr(ctx->device, words, total, 0, 0) == total;
}

// Select which Command2 bank (if any) is active - subsequent
// _dev_st7701_command() register addresses are only meaningful relative
// to whichever bank (or none) is currently selected. See the enum bk0/
// bk1/bk3 register maps in ST7701::command in Pimoroni's own driver -
// picofuse has no need to name every register, only the ones actually
// used below.
static bool _dev_st7701_bk0_enable(pix_display_t *display) {
  return _dev_st7701_command(display, ST7701_CND2BKXSEL, _st7701_bk0_select,
                             sizeof(_st7701_bk0_select));
}
static bool _dev_st7701_bk1_enable(pix_display_t *display) {
  return _dev_st7701_command(display, ST7701_CND2BKXSEL, _st7701_bk1_select,
                             sizeof(_st7701_bk1_select));
}
static bool _dev_st7701_bk3_enable(pix_display_t *display) {
  return _dev_st7701_command(display, ST7701_CND2BKXSEL, _st7701_bk3_select,
                             sizeof(_st7701_bk3_select));
}
static bool _dev_st7701_bkx_disable(pix_display_t *display) {
  return _dev_st7701_command(display, ST7701_CND2BKXSEL, _st7701_bkx_disable,
                             sizeof(_st7701_bkx_disable));
}

// Quadratic ease, matching the perceptual-brightness approach already
// used for NeoPixel brightness (see hw/pico/led_neopixel.c's own
// _hw_led_neopixel_scale()) - smooth and monotonic across the full 0-255
// range, unlike Pimoroni's own set_backlight() curve, which only reaches
// ~15% duty by brightness=254 and then jumps straight to 100% at 255.
static float _dev_st7701_backlight_duty_percent(uint8_t brightness) {
  return ((float)brightness * (float)brightness) / (255.0f * 255.0f) * 100.0f;
}

// Panel bring-up sequence, adapted from Pimoroni's own ST7701::common_init()
// - mostly gamma/voltage/"Forbidden Knowledge" tuning specific to Presto's
// own TL040WVS03CT15-H1263A glass, not documented in the ST7701 datasheet
// itself (LNESET is the one exception - see its own comment below).
static bool _dev_st7701_common_init(pix_display_t *display, pix_size_t size,
                                    const dev_st7701_config_t *config) {
  // Accumulated with short-circuiting `&&`, so a failed transfer both
  // gets reported and stops any further commands from being issued -
  // not just a discarded-result, always-true rubber stamp.
  bool ok = _dev_st7701_command(display, ST7701_SWRESET, NULL, 0);
  sys_sleep_ms(150);

  // NL = (Line[6:0]+1)*8 (ST7701 datasheet 12.3.2.6) - dev_st7701_init()
  // already validated size.h is a positive multiple of 8 within range.
  uint8_t line = (uint8_t)(size.h / 8 - 1);
  ok = ok && _dev_st7701_bk0_enable(display);
  ok = ok && _dev_st7701_command(display, ST7701_BK0_LNESET,
                                 (const uint8_t[]){line, 0x00}, 2);
  ok = ok && _dev_st7701_command(display, ST7701_BK0_PORCTRL,
                                 (const uint8_t[]){0x0d, 0x02}, 2);
  ok = ok && _dev_st7701_command(display, ST7701_BK0_INVSET,
                                 (const uint8_t[]){0x31, 0x01}, 2);
  ok = ok && _dev_st7701_command(display, ST7701_BK0_COLCTRL,
                                 (const uint8_t[]){0x08}, 1);
  ok = ok &&
       _dev_st7701_command(display, ST7701_BK0_PVGAMCTRL,
                           (const uint8_t[]){0x00, 0x11, 0x18, 0x0e, 0x11, 0x06,
                                             0x07, 0x08, 0x07, 0x22, 0x04, 0x12,
                                             0x0f, 0xaa, 0x31, 0x18},
                           16);
  ok = ok &&
       _dev_st7701_command(display, ST7701_BK0_NVGAMCTRL,
                           (const uint8_t[]){0x00, 0x11, 0x19, 0x0e, 0x12, 0x07,
                                             0x08, 0x08, 0x08, 0x22, 0x04, 0x11,
                                             0x11, 0xa9, 0x32, 0x18},
                           16);
  ok = ok && _dev_st7701_command(display, ST7701_BK0_RGBCTRL,
                                 (const uint8_t[]){0x80, 0x2e, 0x0e}, 3);

  ok = ok && _dev_st7701_bk1_enable(display);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_VHRS,
                                 (const uint8_t[]){0x60}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_VCOMS,
                                 (const uint8_t[]){0x32}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_VGHSS,
                                 (const uint8_t[]){0x07}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_TESTCMD,
                                 (const uint8_t[]){0x80}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_VGLS,
                                 (const uint8_t[]){0x49}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_PWCTRL1,
                                 (const uint8_t[]){0x85}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_PWCTRL2,
                                 (const uint8_t[]){0x21}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_PDR1,
                                 (const uint8_t[]){0x78}, 1);
  ok = ok && _dev_st7701_command(display, ST7701_BK1_PDR2,
                                 (const uint8_t[]){0x78}, 1);

  // Undocumented registers required by this specific panel - still within
  // BK1 (no bank switch since the block above). Present verbatim from the
  // reference driver; the display doesn't work without them.
  ok = ok && _dev_st7701_command(display, 0xE0,
                                 (const uint8_t[]){0x00, 0x1b, 0x02}, 3);
  ok = ok &&
       _dev_st7701_command(display, 0xE1,
                           (const uint8_t[]){0x08, 0xa0, 0x00, 0x00, 0x07, 0xa0,
                                             0x00, 0x00, 0x00, 0x44, 0x44},
                           11);
  ok = ok && _dev_st7701_command(display, 0xE2,
                                 (const uint8_t[]){0x11, 0x11, 0x44, 0x44, 0xed,
                                                   0xa0, 0x00, 0x00, 0xec, 0xa0,
                                                   0x00, 0x00},
                                 12);
  ok = ok && _dev_st7701_command(display, 0xE3,
                                 (const uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4);
  ok = ok &&
       _dev_st7701_command(display, 0xE4, (const uint8_t[]){0x44, 0x44}, 2);
  ok = ok &&
       _dev_st7701_command(display, 0xE5,
                           (const uint8_t[]){0x0a, 0xe9, 0xd8, 0xa0, 0x0c, 0xeb,
                                             0xd8, 0xa0, 0x0e, 0xed, 0xd8, 0xa0,
                                             0x10, 0xef, 0xd8, 0xa0},
                           16);
  ok = ok && _dev_st7701_command(display, 0xE6,
                                 (const uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4);
  ok = ok &&
       _dev_st7701_command(display, 0xE7, (const uint8_t[]){0x44, 0x44}, 2);
  ok = ok &&
       _dev_st7701_command(display, 0xE8,
                           (const uint8_t[]){0x09, 0xe8, 0xd8, 0xa0, 0x0b, 0xea,
                                             0xd8, 0xa0, 0x0d, 0xec, 0xd8, 0xa0,
                                             0x0f, 0xee, 0xd8, 0xa0},
                           16);
  ok =
      ok && _dev_st7701_command(
                display, 0xEB,
                (const uint8_t[]){0x02, 0x00, 0xe4, 0xe4, 0x88, 0x00, 0x40}, 7);
  ok = ok &&
       _dev_st7701_command(display, 0xEC, (const uint8_t[]){0x3c, 0x00}, 2);
  ok = ok &&
       _dev_st7701_command(display, 0xED,
                           (const uint8_t[]){0xab, 0x89, 0x76, 0x54, 0x02, 0xff,
                                             0xff, 0xff, 0xff, 0xff, 0xff, 0x20,
                                             0x45, 0x67, 0x98, 0xba},
                           16);
  ok = ok &&
       _dev_st7701_command(display, ST7701_MADCTL, (const uint8_t[]){0x00}, 1);

  ok = ok && _dev_st7701_bk3_enable(display);
  ok = ok && _dev_st7701_command(display, 0xE5, (const uint8_t[]){0xe4}, 1);

  ok = ok && _dev_st7701_bkx_disable(display);
  ok = ok &&
       _dev_st7701_command(display, ST7701_COLMOD, (const uint8_t[]){0x66}, 1);

  // Rotation - only 0/180 supported, matching the reference driver.
  // Mirrors the reference's own set_rotation(), just inlined rather than
  // exposed as its own public entry point yet.
  uint8_t madctl = 0x00; // Mirror Y off, no RGB swap
  uint8_t sdir = 0x00;   // Mirror X off
  if (config->rotation == 180) {
    madctl = 0x10;
    sdir = 0x04;
  }
  ok = ok && _dev_st7701_command(display, ST7701_MADCTL, &madctl, 1);
  ok = ok && _dev_st7701_bk0_enable(display);
  ok = ok && _dev_st7701_command(display, ST7701_BK0_SDIR, &sdir, 1);
  ok = ok && _dev_st7701_bkx_disable(display);

  ok = ok && _dev_st7701_command(display, ST7701_INVON, NULL, 0);
  sys_sleep_ms(1);
  ok = ok && _dev_st7701_command(display, ST7701_SLPOUT, NULL, 0);
  sys_sleep_ms(120);
  ok = ok && _dev_st7701_command(display, ST7701_DISPON, NULL, 0);
  sys_sleep_ms(50);
  return ok;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Fill an ST7701 config struct with safe defaults.
 *  @ingroup ST7701
 */
void dev_st7701_default_config(dev_st7701_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->rotation = 0;
}

/** @brief Initialize an ST7701-driven display over SPI.
 *  @ingroup ST7701
 *  @param device SPI device handle. This panel has no D/CX GPIO - `device`
 * must be a hw_spi_init() handle configured with `bits_per_word = 9`,
 * command/data select packed as the 9th bit of every word (see
 * hw_deviceio_xfr()).
 *  @param size Panel resolution. `size.w` just needs to be nonzero - the
 * current SPI-only bring-up sequence doesn't otherwise depend on it (see
 * this file's own CONSTANTS comment). `size.h` must be a positive
 * multiple of 8, up to 1024, and is used to compute the LNESET register
 * directly.
 *  @param bl_pin Optional GPIO handle for the backlight. Pass `NULL` if
 * the backlight isn't software-controlled.
 *  @param config Optional pointer to initialization options. Pass `NULL`
 * to use default values.
 *  @return display handle, or `NULL` on failure
 */
pix_display_t *dev_st7701_init(hw_deviceio_t *device, pix_size_t size,
                               hw_gpio_t *bl_pin,
                               const dev_st7701_config_t *config) {
  if (device == NULL || size.w == 0 || size.h == 0 || size.h % 8 != 0 ||
      size.h > ST7701_LNESET_MAX_HEIGHT) {
    sys_debugf("st7701", "dev_st7701_init: invalid arguments");
    return NULL;
  }

  dev_st7701_config_t settings;
  if (config == NULL) {
    dev_st7701_default_config(&settings);
  } else {
    settings = *config;
  }

  if (settings.rotation != 0 && settings.rotation != 180) {
    sys_debugf("st7701", "dev_st7701_init: unsupported rotation");
    return NULL;
  }

  // Backlight is optional - wrap it as PWM
  hw_pwm_t *bl_pwm = NULL;
  if (bl_pin != NULL) {
    hw_gpio_set_mode(bl_pin, hw_gpio_pwm);
    // ~32.25kHz, matching Pimoroni's own reference (TOP=6200 against
    // their 200MHz sys clock) rather than an arbitrary 1kHz - the AP3031
    // backlight driver's dimming behaviour at low duty may be frequency
    // sensitive.
    hw_pwm_config_t bl_config = {
        .period_ns = 31000u,
        .duty_percent = 0.0f,
        .enabled = true,
    };
    bl_pwm = hw_pwm_init(bl_pin, NULL, NULL, &bl_config);
    if (bl_pwm == NULL) {
      sys_debugf("st7701", "dev_st7701_init: failed to init backlight PWM");
      return NULL;
    }
  }

  // Allocate memory for the display
  pix_display_t *display =
      _pix_display_alloc(&_dev_st7701_ops, size, PIX_FMT_RGB565);
  if (display == NULL) {
    if (bl_pwm != NULL) {
      hw_pwm_deinit(bl_pwm);
    }
    return NULL;
  }

  // Create the driver context
  _dev_st7701_ctx_t *ctx = _pix_display_context(display);
  ctx->device = device;
  ctx->bl_pwm = bl_pwm;

  // Initalize the hardware
  if (!_dev_st7701_common_init(display, size, &settings)) {
    sys_debugf("st7701", "dev_st7701_init: panel bring-up failed");
    if (bl_pwm != NULL) {
      hw_pwm_deinit(bl_pwm);
    }
    _pix_display_free(display);
    return NULL;
  }

  // Switch on the screen (full brightness)
  if (bl_pwm != NULL) {
    sys_sleep_ms(50); // Let the panel settle before lighting it up
    hw_pwm_set_duty_percent(bl_pwm, _dev_st7701_backlight_duty_percent(255));
  }

  // TODO: pixel data path (parallel RGB666 bus, PIO-driven scanout) -
  // this panel has no SPI framebuffer path. Not yet implemented.
  return display;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @brief Set the backlight brightness.
 *  @ingroup ST7701
 */
bool dev_st7701_set_backlight(pix_display_t *display, uint8_t brightness) {
  if (display == NULL || display->ops != &_dev_st7701_ops) {
    return false;
  }
  _dev_st7701_ctx_t *ctx = _pix_display_context(display);
  if (ctx->bl_pwm == NULL) {
    return false;
  }
  return hw_pwm_set_duty_percent(
      ctx->bl_pwm, _dev_st7701_backlight_duty_percent(brightness));
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - OPS

/** Not yet implemented - see dev_st7701_init()'s own TODO. */
static pix_bitmap_t *_dev_st7701_lock(pix_display_t *display) {
  (void)display;
  return NULL;
}

/** Not yet implemented - see dev_st7701_init()'s own TODO. */
static void _dev_st7701_unlock(pix_display_t *display) { (void)display; }

/** Not yet implemented - see dev_st7701_init()'s own TODO. */
static bool _dev_st7701_poll(pix_display_t *display) {
  (void)display;
  return false;
}

static void _dev_st7701_deinit(pix_display_t *display) {
  _dev_st7701_ctx_t *ctx = _pix_display_context(display);
  if (ctx->bl_pwm != NULL) {
    hw_pwm_deinit(ctx->bl_pwm);
  }
}
