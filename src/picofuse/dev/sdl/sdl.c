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

// Creates ctx->renderer and, on top of it, ctx->texture (a streaming
// texture, ready for a future _dev_sdl_lock() to SDL_LockTexture()) sized
// and formatted from display->bitmap.size/display->bitmap.fmt (the latter
// translated to SDL's own enum here - see _dev_sdl_pixel_format()). Reads
// size/format from there rather than taking its own copy, since
// _pix_display_alloc() already seeded them and they never change after -
// see pix_display_t::bitmap's own doc. ctx->window must already exist.
// Leaves both NULL and returns false on failure - never partially
// succeeds (a renderer with no texture is cleaned up before returning).
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
      SDL_CreateTexture(ctx->renderer, sdl_format, SDL_TEXTUREACCESS_STREAMING,
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

// Locks ctx->texture for direct pixel access and updates display->bitmap
// (size/fmt are already set - see pix_display_t::bitmap's own doc) with
// the result, so the caller (only ever _pix_display_draw(), immediately
// before it runs the display's draw callback - see pix_display_ops_t::lock's
// own doc) gets a bitmap that's ready to draw into.
static pix_bitmap_t *_dev_sdl_lock(pix_display_t *display) {
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  void *pixels = NULL;
  int pitch = 0;
  if (SDL_LockTexture(ctx->texture, NULL, &pixels, &pitch) != 0) {
    sys_debugf("sdl", "_dev_sdl_lock: SDL_LockTexture failed: %s",
               SDL_GetError());
    return NULL;
  }
  display->bitmap.data = pixels;
  display->bitmap.stride = (size_t)pitch;
  return &display->bitmap;
}

// Unlocks ctx->texture, clears display's dirty state (see
// pix_display_ops_t::unlock's own doc), and presents the result to the
// window.
static void _dev_sdl_unlock(pix_display_t *display) {
  _dev_sdl_ctx_t *ctx = _pix_display_context(display);
  SDL_UnlockTexture(ctx->texture);
  display->bitmap.data = NULL;
  display->bitmap.stride = 0;

  // _dev_sdl_lock() always locks the whole texture (a NULL rect), so every
  // flush clears the dirty rect entirely rather than shrinking it - there's
  // no partial-region locking yet to narrow it instead.
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

  // @todo _dev_sdl_lock()/_dev_sdl_unlock() don't actually use ctx->texture
  // yet (see _dev_sdl_lock()'s own @todo), so nothing reaches the window's
  // own pixels. The display handle is otherwise fully wired:
  // _dev_sdl_deinit() already destroys the texture/renderer and window and
  // tears the subsystem down correctly once this (or any other SDL
  // display) is freed.
  return display;
}
