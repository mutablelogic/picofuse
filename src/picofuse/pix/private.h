#pragma once
#include <picofuse/pix.h>

///////////////////////////////////////////////////////////////////////////////
// LOCK
//
// Guards the display pool below - shared by display.c (alloc/free) and
// poll.c (pix_poll()'s own scan for the display due for service), so both
// need to lock it the same way. Same host-vs-Pico split as hw_deviceio's
// own pool (see src/picofuse/hw/deviceio/deviceio.c) - locked on host
// platforms too, not just Pico, since Linux/Darwin both support real
// multi-threaded callers.
#ifdef SYSTEM_NAME_PICO
#include "../sys/pico/sync.h"
#define _PIX_DISPLAY_LOCK() _sys_sync_pool_lock()
#define _PIX_DISPLAY_UNLOCK() _sys_sync_pool_unlock()
#else
#include <pthread.h>
// Defined once in poll.c; every translation unit that includes this header
// (display.c too) links against that same instance - see hw_led's own
// _hw_led_lock (src/picofuse/hw/led/private.h) for the identical shape.
extern pthread_mutex_t _pix_display_lock;
#define _PIX_DISPLAY_LOCK() pthread_mutex_lock(&_pix_display_lock)
#define _PIX_DISPLAY_UNLOCK() pthread_mutex_unlock(&_pix_display_lock)
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Display operations structure.
 *
 * The vtable a backend (e.g. dev/sdl.h) hands to `_pix_display_alloc()`.
 * Shared across every display that backend creates, so it must stay
 * stateless - per-display state (dirty rectangle, update timestamp) lives
 * in `pix_display_t` itself, not here. Private to the backends that
 * implement one and to this pool - never exposed to public API callers,
 * same as hw_deviceio_ops_t/hw_led_ops_t in src/picofuse/hw.
 */
typedef struct pix_display_ops_t {
  /**
   * @brief Lock the display for direct pixel access.
   * @param display The display to lock.
   * @return Pointer to the locked bitmap, or NULL on failure.
   *
   * Only ever called by _pix_display_poll() itself, immediately before it
   * calls the display's own draw callback (see pix_display_set_callback())
   * - never exposed as public API, so a backend is free to assume lock/
   * draw/unlock always happen back to back on the same thread, with no
   * other call to lock() in between.
   */
  pix_bitmap_t *(*lock)(pix_display_t *display);

  /**
   * @brief Unlock the display after direct pixel access.
   * @param display The display to unlock.
   *
   * Only ever called by _pix_display_poll(), right after the draw callback
   * returns - see `lock` above. Responsible for clearing (or shrinking)
   * `display->dirty_origin`/`display->dirty_size` to reflect whatever it
   * actually flushed to the real screen - a backend that only manages a
   * partial flush should narrow the rect rather than clear it outright, so
   * the remainder stays known-dirty for next time.
   */
  void (*unlock)(pix_display_t *display);

  /**
   * @brief Poll the display for events, and report whether it's due a
   * redraw.
   * @param display The display to poll.
   * @return `true` if _pix_display_poll() should lock, run the draw
   * callback, and unlock this display; `false` to skip that this round.
   */
  bool (*poll)(pix_display_t *display);

  /**
   * @brief Deinitialize the display and release resources.
   * @param display The display to deinitialize.
   */
  void (*deinit)(pix_display_t *display);
} pix_display_ops_t;

/**
 * @brief Display handle (shared across all backends).
 *
 * Deliberately private to this module - a backend never sees this layout,
 * only the `ops` vtable it hands to `_pix_display_alloc()` and the embedded
 * `context` scratch buffer it gets back via `_pix_display_context()`.
 * Handles come from a small fixed-size pool (@ref PIX_DISPLAY_POOL_CAPACITY,
 * see display.c), not the heap - the same shape as hw_deviceio_t/hw_led_t
 * in src/picofuse/hw.
 */
struct pix_display_t {
  const pix_display_ops_t *ops;

  // Rectangle which is dirty (needs to be flushed to the display). Cleared
  // (or shrunk) by a backend's own ops->unlock() once it's actually
  // flushed - see `pix_display_ops_t::unlock`'s own doc. @todo Nothing
  // consults this yet to decide "due" - a backend's own ops->poll() decides
  // purely on time for now (see e.g. dev/sdl/sdl.c's _dev_sdl_poll());
  // checking this too, so an undamaged display doesn't redraw just because
  // its interval elapsed, comes next.
  pix_point_t dirty_origin;
  pix_size_t dirty_size;

  // Timestamp of the last time this display was found due, in ms - set by
  // _pix_display_poll() as soon as ops->poll() reports due, whether or not
  // a lock/draw/unlock round actually follows (e.g. no draw callback is
  // registered, or ops->lock() itself fails) - otherwise a backend with no
  // working lock/unlock yet would report "due" again on every single poll
  // forever, instead of respecting its own rate limit. Read (but never
  // written) by a backend's own ops->poll(), alongside whatever rate limit
  // it keeps in its own context, to decide whether it's due a redraw - see
  // e.g. dev/sdl/sdl.c's _dev_sdl_poll().
  uint64_t ts;

  // App-supplied draw callback and its opaque userdata, set via
  // pix_display_set_callback() - NULL until an app registers one.
  // _pix_display_poll() calls this (with an already-locked bitmap) whenever
  // ops->poll() reports the display due and a callback is registered.
  pix_display_draw_t draw;
  void *draw_userdata;

  // The display's own bitmap descriptor - `size` and `fmt` are seeded once,
  // at `_pix_display_alloc()` time, and stay fixed for the display's
  // lifetime; `data`/`stride` are a backend's own ops->lock() to fill in
  // (from whatever real pixel storage it owns - a locked SDL_Texture, an
  // e-ink panel's framebuffer, ...) and are only meaningful between a
  // lock()/unlock() pair. Living here rather than in a backend's own
  // context means every backend's lock() can hand back `&display->bitmap`
  // itself instead of keeping its own separate copy of `size`/`fmt` - see
  // dev/sdl/sdl.c's own _dev_sdl_lock() for the shape.
  pix_bitmap_t bitmap;

  _Alignas(max_align_t) uint8_t context[PIX_DISPLAY_CONTEXT_SIZE];
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Not static - poll.c's own pix_poll() iterates this too.
extern pix_display_t _pix_display_pool[PIX_DISPLAY_POOL_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS (see display.c)

/**
 * @brief Claim a display handle from the fixed pool.
 *
 * The returned handle has a zeroed context buffer and zeroed `ts`, but its
 * dirty rectangle is seeded to the full @p size, at origin (0,0) - so the
 * new display is due a flush from the moment it's created, even before
 * anything has actually drawn to it. `bitmap.size`/`bitmap.fmt` are seeded
 * from @p size/@p format too (`bitmap.data`/`bitmap.stride` start NULL/0 -
 * a backend's own ops->lock() fills those in each time). @p ops must stay
 * valid until `_pix_display_free()` releases the handle.
 * @param ops Backend vtable to bind the handle to.
 * @param size The display's full size.
 * @param format The display's pixel format.
 * @return A handle bound to @p ops, or `NULL` if @p ops is `NULL` or the
 * pool is full.
 */
pix_display_t *_pix_display_alloc(const pix_display_ops_t *ops,
                                  pix_size_t size, pix_format_t format);

/** @brief Return a display handle's backend-private context, or NULL when
 * the handle is invalid. */
void *_pix_display_context(const pix_display_t *display);

/**
 * @brief Release a display handle back to the pool.
 *
 * Only releases the pool slot - a backend-specific `ops->deinit()` (if
 * needed) is the caller's responsibility to invoke first, before freeing
 * the handle.
 */
void _pix_display_free(pix_display_t *display);

/**
 * @brief Poll a display, and run its draw callback if it's due a redraw.
 *
 * Calls `ops->poll()`; if that reports the display due, stamps `ts`
 * immediately (see `pix_display_t::ts`'s own doc on why that happens
 * unconditionally rather than only after a successful draw). Then, only if
 * a draw callback is registered (see `pix_display_set_callback()`) and
 * `ops->lock()`/`ops->unlock()` are both implemented and `ops->lock()`
 * actually returns a bitmap, runs the callback and unlocks. A no-op if
 * @p display is invalid. Only ever called from poll.c's own `pix_poll()` -
 * not public API, since an app has no way to name a specific display to
 * poll; it only ever polls "whichever display is due" via `pix_poll()`.
 * @param display The display to poll.
 */
void _pix_display_poll(pix_display_t *display);
