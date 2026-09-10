#include "../../pix/private.h"
#include <SDL.h>
#include <picofuse/dev/sdl.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Backend-private per-display state
typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  // Minimum time between flushes, in ms - `0` for no rate limit
  uint16_t interval_ms;
} _dev_sdl_ctx_t;

_Static_assert(sizeof(_dev_sdl_ctx_t) <= PIX_DISPLAY_CONTEXT_SIZE,
               "_dev_sdl_ctx_t exceeds PIX_DISPLAY_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Retain count gating the shared SDL subsystem (SDL_Init/SDL_Quit)
static sys_atomic_t _dev_sdl_retain = {0};

///////////////////////////////////////////////////////////////////////////////
// OPS DECLARATIONS

static pix_bitmap_t *_dev_sdl_lock(pix_display_t *display);
static void _dev_sdl_unlock(pix_display_t *display);
static bool _dev_sdl_poll(pix_display_t *display);
static void _dev_sdl_deinit(pix_display_t *display);

static const pix_display_ops_t _dev_sdl_ops = {
    .lock = _dev_sdl_lock,
    .unlock = _dev_sdl_unlock,
    .poll = _dev_sdl_poll,
    .deinit = _dev_sdl_deinit,
};

static pix_color_t _dev_sdl_bitmap_get_pixel(const pix_bitmap_t *bitmap,
                                             pix_point_t point);
static void _dev_sdl_bitmap_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                      pix_color_t color);
static void _dev_sdl_bitmap_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                      pix_size_t size, pix_color_t color);

// GPU-accelerated ops for a bitmap returned by _dev_sdl_lock() - this
// display's ctx->texture is SDL_TEXTUREACCESS_TARGET (see
// _dev_sdl_create_texture()), which can't be SDL_LockTexture()'d for direct
// CPU access at all, so bitmap->data stays NULL the whole time it's locked
// and the pix_bitmap_*() direct-memory fallback has nothing to fall back
// to - every op here exists to cover for that.
static const pix_bitmap_ops_t _dev_sdl_bitmap_ops = {
    .get_pixel = _dev_sdl_bitmap_get_pixel,
    .set_pixel = _dev_sdl_bitmap_set_pixel,
    .fill_rect = _dev_sdl_bitmap_fill_rect,
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Maps a pix_format_t to the matching SDL_PixelFormatEnum, or
// SDL_PIXELFORMAT_UNKNOWN if this backend has no matching SDL format yet.
static SDL_PixelFormatEnum _dev_sdl_pixel_format(pix_format_t format) {
  switch (format) {
  case PIX_FMT_RGBA32:
    return SDL_PIXELFORMAT_RGBA32;
  case PIX_FMT_RGB888:
    return SDL_PIXELFORMAT_RGB24;
  case PIX_FMT_RGB565:
    return SDL_PIXELFORMAT_RGB565;
  case PIX_FMT_MONO:
    break; // unsupported - see this function's own doc
  }
  return SDL_PIXELFORMAT_UNKNOWN;
}

// Creates ctx->renderer and, on top of it, ctx->texture (a render-target
// texture, ready for a future _dev_sdl_lock() to SDL_SetRenderTarget() onto
// - see its own doc on why this backend uses render-target rather than
// streaming/lock-based texture access) sized and formatted from
// display->bitmap.size/display->bitmap.fmt (the latter translated to SDL's
// own enum here - see _dev_sdl_pixel_format()). Reads size/format from
// there rather than taking its own copy, since _pix_display_alloc() already
// seeded them and they never change after - see pix_display_t::bitmap's
// own doc. ctx->window must already exist. Leaves both NULL and returns
// false on failure - never partially succeeds (a renderer with no texture
// is cleaned up before returning).
static bool _dev_sdl_create_texture(pix_display_t *display) {
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  SDL_PixelFormatEnum sdl_format = _dev_sdl_pixel_format(display->bitmap.fmt);
  if (sdl_format == SDL_PIXELFORMAT_UNKNOWN) {
    sys_debugf("sdl",
               "_dev_sdl_create_texture: no SDL pixel format for "
               "pix_format_t %d",
               (int)display->bitmap.fmt);
    return false;
  }

  ctx->renderer = SDL_CreateRenderer(ctx->window, -1, 0);
  if (ctx->renderer == NULL) {
    sys_debugf("sdl", "_dev_sdl_create_texture: SDL_CreateRenderer failed: %s",
               SDL_GetError());
    return false;
  }

  ctx->texture =
      SDL_CreateTexture(ctx->renderer, sdl_format, SDL_TEXTUREACCESS_TARGET,
                        display->bitmap.size.w, display->bitmap.size.h);
  if (ctx->texture == NULL) {
    sys_debugf("sdl", "_dev_sdl_create_texture: SDL_CreateTexture failed: %s",
               SDL_GetError());
    SDL_DestroyRenderer(ctx->renderer);
    ctx->renderer = NULL;
    return false;
  }

  return true;
}

// Reverses _dev_sdl_create_texture() - safe to call whether or not it
// (fully) succeeded, since it always leaves ctx->texture/ctx->renderer
// either both set or both NULL. Destroys the texture before the renderer
// it belongs to, and the renderer before ctx->window itself gets destroyed
// by the caller - SDL requires child objects torn down before their parent.
static void _dev_sdl_destroy_texture(_dev_sdl_ctx_t *ctx) {
  if (ctx->texture != NULL) {
    SDL_DestroyTexture(ctx->texture);
    ctx->texture = NULL;
  }
  if (ctx->renderer != NULL) {
    SDL_DestroyRenderer(ctx->renderer);
    ctx->renderer = NULL;
  }
}

// GPU-accelerated get_pixel for a bitmap _dev_sdl_lock() returned - reads
// back the single pixel via SDL_RenderReadPixels() (only ever valid while
// @p bitmap's display has ctx->texture set as the active render target,
// i.e. between a lock()/unlock() pair - see _dev_sdl_lock()'s own doc),
// then hands the raw bytes to pix_bitmap_get_pixel() on a throwaway
// one-pixel bitmap, so the actual byte-order decode reuses the same
// per-format logic as rgba32.c/rgb888.c/rgb565.c rather than duplicating
// it here.
static pix_color_t _dev_sdl_bitmap_get_pixel(const pix_bitmap_t *bitmap,
                                             pix_point_t point) {
  pix_display_t *display = _pix_bitmap_display(bitmap);
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);

  uint8_t pixel[4] = {0}; // large enough for any format this backend maps
  SDL_Rect rect = {.x = point.x, .y = point.y, .w = 1, .h = 1};
  if (SDL_RenderReadPixels(ctx->renderer, &rect,
                           _dev_sdl_pixel_format(bitmap->fmt), pixel,
                           sizeof(pixel)) != 0) {
    sys_debugf("sdl",
              "_dev_sdl_bitmap_get_pixel: SDL_RenderReadPixels failed: %s",
              SDL_GetError());
    return PIX_COLOR_NONE;
  }

  pix_bitmap_t tmp = {.data = pixel,
                      .size = {.w = 1, .h = 1},
                      .stride = sizeof(pixel),
                      .fmt = bitmap->fmt,
                      .ops = NULL};
  return pix_bitmap_get_pixel(&tmp, (pix_point_t){0});
}

// GPU-accelerated set_pixel for a bitmap _dev_sdl_lock() returned - only
// ever called while @p bitmap's display has ctx->texture set as the active
// render target (i.e. between a lock()/unlock() pair), so this draws
// straight onto it rather than the window - see _dev_sdl_lock()'s own doc.
static void _dev_sdl_bitmap_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                      pix_color_t color) {
  pix_display_t *display = _pix_bitmap_display(bitmap);
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);

  // PIX_BLEND (bitmap->op's default - see pix_bitmap_t::op's own doc) lets
  // SDL's own GPU blending do the alpha-compositing math, rather than
  // reading back the destination pixel and blending it ourselves the way
  // the direct-memory formats (rgba32.c/rgb888.c/rgb565.c) have to.
  SDL_SetRenderDrawBlendMode(ctx->renderer, bitmap->op == PIX_BLEND
                                                ? SDL_BLENDMODE_BLEND
                                                : SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(ctx->renderer, pix_color_r(color), pix_color_g(color),
                         pix_color_b(color), pix_color_a(color));
  if (SDL_RenderDrawPoint(ctx->renderer, point.x, point.y) != 0) {
    sys_debugf("sdl", "_dev_sdl_bitmap_set_pixel: SDL_RenderDrawPoint failed: %s",
              SDL_GetError());
  }
}

// GPU-accelerated fill_rect for a bitmap _dev_sdl_lock() returned - only
// ever called while @p bitmap's display has ctx->texture set as the active
// render target (i.e. between a lock()/unlock() pair), so this draws
// straight onto it rather than the window - see _dev_sdl_lock()'s own doc.
static void _dev_sdl_bitmap_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                      pix_size_t size, pix_color_t color) {
  pix_display_t *display = _pix_bitmap_display(bitmap);
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);

  // See _dev_sdl_bitmap_set_pixel()'s own doc on PIX_BLEND vs PIX_SET here.
  SDL_SetRenderDrawBlendMode(ctx->renderer, bitmap->op == PIX_BLEND
                                                ? SDL_BLENDMODE_BLEND
                                                : SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(ctx->renderer, pix_color_r(color), pix_color_g(color),
                         pix_color_b(color), pix_color_a(color));

  SDL_Rect rect = {
      .x = origin.x, .y = origin.y, .w = size.w, .h = size.h};
  if (SDL_RenderFillRect(ctx->renderer, &rect) != 0) {
    sys_debugf("sdl", "_dev_sdl_bitmap_fill_rect: SDL_RenderFillRect failed: %s",
              SDL_GetError());
  }
}

// Makes ctx->texture the active render target, so draw calls made during
// the display's own draw callback (SDL_RenderFillRect() via
// _dev_sdl_bitmap_fill_rect(), for now) land on it rather than the window.
// SDL_TEXTUREACCESS_TARGET textures can't be SDL_LockTexture()'d at all -
// see _dev_sdl_create_texture()'s own doc - so display->bitmap.data/stride
// stay NULL/0 the whole time this display is locked, unlike a
// direct-memory backend's own lock(). display->bitmap.ops is set to
// _dev_sdl_bitmap_ops for the same reason: with no CPU pointer to fall
// back to, drawing only works through what that vtable actually covers.
static pix_bitmap_t *_dev_sdl_lock(pix_display_t *display) {
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  if (SDL_SetRenderTarget(ctx->renderer, ctx->texture) != 0) {
    sys_debugf("sdl", "_dev_sdl_lock: SDL_SetRenderTarget failed: %s",
               SDL_GetError());
    return NULL;
  }
  display->bitmap.ops = &_dev_sdl_bitmap_ops;
  return &display->bitmap;
}

// Restores the window as the active render target, clears display's dirty
// state (see pix_display_ops_t::unlock's own doc), and presents the
// texture just drawn into to the window.
static void _dev_sdl_unlock(pix_display_t *display) {
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  SDL_SetRenderTarget(ctx->renderer, NULL);
  display->bitmap.ops = NULL;

  // _dev_sdl_lock() always makes the whole texture available for drawing,
  // so every flush clears the dirty rect entirely rather than shrinking it
  // - there's no partial-region tracking yet to narrow it instead.
  display->dirty_origin = (pix_point_t){0};
  display->dirty_size = (pix_size_t){0};

  if (SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL) != 0) {
    sys_debugf("sdl", "_dev_sdl_unlock: SDL_RenderCopy failed: %s",
               SDL_GetError());
    return;
  }
  SDL_RenderPresent(ctx->renderer);
}

/**
 * @brief Poll an SDL display for events and determine if it needs a redraw.
 * @param display The display to poll.
 * @return `true` if the display should be redrawn, `false` otherwise.
 */
static bool _dev_sdl_poll(pix_display_t *display) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    sys_debugf("sdl", "_dev_sdl_poll: SDL event received (type=%u)",
               (unsigned)event.type);
  }

  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  uint64_t now = sys_timestamp_ms();
  // ts == 0 means never drawn yet
  if (display->ts != 0 && now - display->ts < ctx->interval_ms) {
    return false;
  }
  return true;
}

static void _dev_sdl_deinit(pix_display_t *display) {
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  _dev_sdl_destroy_texture(ctx);
  if (ctx->window != NULL) {
    SDL_DestroyWindow(ctx->window);
  }

  // only tear the subsystem down once the last display using it has gone.
  if (sys_atomic_dec(&_dev_sdl_retain) == 0) {
    SDL_Quit();
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

// See dev/sdl.h for the public doc.
pix_display_t *dev_sdl_init(const char *title, pix_size_t size,
                            pix_format_t format, dev_sdl_flags_t flags,
                            uint16_t interval_ms) {
  if (sys_atomic_inc(&_dev_sdl_retain) == 1) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      sys_debugf("sdl", "dev_sdl_init: SDL_Init failed: %s", SDL_GetError());
      sys_atomic_dec(&_dev_sdl_retain);
      return NULL;
    }
  }

  pix_display_t *display = _pix_display_alloc(&_dev_sdl_ops, size, format);
  if (display == NULL) {
    sys_debugf("sdl", "dev_sdl_init: display pool exhausted");
    if (sys_atomic_dec(&_dev_sdl_retain) == 0) {
      SDL_Quit();
    }
    return NULL;
  }

  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  ctx->interval_ms = interval_ms;

  uint32_t window_flags = 0;
  if (flags & dev_sdl_fullscreen) {
    window_flags |= SDL_WINDOW_FULLSCREEN;
  }
  if (flags & dev_sdl_borderless) {
    window_flags |= SDL_WINDOW_BORDERLESS;
  }
  if (flags & dev_sdl_hidden) {
    window_flags |= SDL_WINDOW_HIDDEN;
  }
  int pos = (flags & dev_sdl_centered) ? SDL_WINDOWPOS_CENTERED
                                       : SDL_WINDOWPOS_UNDEFINED;

  ctx->window = SDL_CreateWindow(title != NULL ? title : sys_env_name(), pos,
                                 pos, size.w, size.h, window_flags);
  if (ctx->window == NULL) {
    sys_debugf("sdl", "dev_sdl_init: SDL_CreateWindow failed: %s",
               SDL_GetError());
    _pix_display_free(display);
    if (sys_atomic_dec(&_dev_sdl_retain) == 0) {
      SDL_Quit();
    }
    return NULL;
  }

  if (!_dev_sdl_create_texture(display)) {
    SDL_DestroyWindow(ctx->window);
    _pix_display_free(display);
    if (sys_atomic_dec(&_dev_sdl_retain) == 0) {
      SDL_Quit();
    }
    return NULL;
  }

  // The display handle is fully wired: _dev_sdl_deinit() already destroys
  // the texture/renderer and window and tears the subsystem down correctly
  // once this (or any other SDL display) is freed.
  return display;
}
