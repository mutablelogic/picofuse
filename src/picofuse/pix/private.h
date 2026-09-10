#pragma once
#include <picofuse/pix.h>
#include <picofuse/sys/atomic.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// LOCK

#ifdef SYSTEM_NAME_PICO
#include "../sys/pico/sync.h"
#define _PIX_DISPLAY_LOCK() _sys_sync_pool_lock()
#define _PIX_DISPLAY_UNLOCK() _sys_sync_pool_unlock()
#else
#include <pthread.h>
extern pthread_mutex_t _pix_display_lock;
#define _PIX_DISPLAY_LOCK() pthread_mutex_lock(&_pix_display_lock)
#define _PIX_DISPLAY_UNLOCK() pthread_mutex_unlock(&_pix_display_lock)
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Backend-specific bitmap operations - see `pix_bitmap_t::ops`'s own
 * doc. A NULL entry means "no accelerated version" - the matching
 * `pix_bitmap_*()` public function falls back to its own default instead
 * (a plain direct-memory path for `get_pixel`/`set_pixel`; repeated
 * `set_pixel` calls, so still routed back through `ops->set_pixel` if that
 * one *is* implemented, for `fill_rect`).
 */
struct pix_bitmap_ops_t {
  pix_color_t (*get_pixel)(const pix_bitmap_t *bitmap, pix_point_t point);
  void (*set_pixel)(pix_bitmap_t *bitmap, pix_point_t point, pix_color_t color);
  void (*fill_rect)(pix_bitmap_t *bitmap, pix_point_t origin, pix_size_t size,
                    pix_color_t color);
};

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
   * Only ever called by poll.c's own _pix_display_draw(), immediately
   * before it calls the display's own draw callback (see
   * pix_display_set_callback()) - never exposed as public API, so a
   * backend is free to assume lock/draw/unlock always happen back to back
   * on the same thread, with no other call to lock() in between.
   */
  pix_bitmap_t *(*lock)(pix_display_t *display);

  /**
   * @brief Unlock the display after direct pixel access.
   * @param display The display to unlock.
   *
   * Only ever called by _pix_display_draw(), right after the draw callback
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
   * @return `true` if this display is due a redraw.
   *
   * Called by poll.c's own pix_poll() for every allocated display, on
   * every call - not just whichever one ends up drawn that round. This is
   * what should pump a backend's own event queue (SDL's, say), since every
   * open display needs that regardless of whose turn it is to redraw; see
   * e.g. dev/sdl/sdl.c's _dev_sdl_poll(). Returning `true` doesn't
   * guarantee `_pix_display_draw()` runs this round - pix_poll() only
   * draws the fairest (longest-waiting) of however many displays report
   * due in the same pass, so the rest just wait their turn.
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

  // Dirty rectangle
  pix_point_t dirty_origin;
  pix_size_t dirty_size;

  // Timestamp of the last time this display was actually drawn - stamped
  // by pix_poll() only for whichever display it picks to draw each round,
  // never for a display that merely reported due but lost out to a fairer
  // (longer-waiting) one in the same pass. That's what keeps multiple due
  // displays taking fair turns instead of one starving the others: an
  // unselected-but-due display keeps its old (smaller) `ts`, so it's first
  // in line next round. Read (but never written) by a backend's own
  // ops->poll(), alongside whatever rate limit it keeps in its own
  // context, to decide whether it's due at all - see e.g. dev/sdl/sdl.c's
  // _dev_sdl_poll().
  uint64_t ts;

  // App-supplied draw callback and its opaque userdata - guarded by
  // _PIX_DISPLAY_LOCK (both written together by pix_display_set_callback(),
  // read together by _pix_display_draw()), so a concurrent callback change
  // can never pair a new `draw` with a stale `draw_userdata`, or leave
  // _pix_display_draw() calling through a `draw` that's since gone stale.
  pix_display_draw_t draw;
  void *draw_userdata;

  // The display's own bitmap descriptor
  pix_bitmap_t bitmap;

  // True (nonzero) while poll.c's own pix_poll() has this display checked
  // out. sys_atomic_t rather than a plain bool guarded by _PIX_DISPLAY_LOCK:
  // pix_poll()'s clear and pix_display_deinit()'s wait only ever touch this
  // one field, so neither needs the pool lock at all - only
  // _pix_poll_next()'s claim does (it's already holding the lock for
  // ops/ts), and an atomic read/write is safe to mix with that regardless.
  sys_atomic_t polling;

  _Alignas(max_align_t) uint8_t context[PIX_DISPLAY_CONTEXT_SIZE];
};

/** @brief Recovers the owning display from one of its own `bitmap` field -
 * valid only for a `pix_bitmap_t*` a backend already knows came from
 * `&display->bitmap` (e.g. what `ops->lock()` returned - see
 * `pix_display_t::bitmap`'s own doc). Lets a `pix_bitmap_ops_t` callback,
 * which only ever receives a `pix_bitmap_t*`, reach back to display-level
 * state it needs - e.g. dev/sdl/sdl.c's own fill_rect, which needs its
 * renderer. */
static inline pix_display_t *_pix_bitmap_display(const pix_bitmap_t *bitmap) {
  return (pix_display_t *)((const uint8_t *)bitmap -
                           offsetof(pix_display_t, bitmap));
}

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Not static - poll.c's own pix_poll() iterates this too.
extern pix_display_t _pix_display_pool[PIX_DISPLAY_POOL_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

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
pix_display_t *_pix_display_alloc(const pix_display_ops_t *ops, pix_size_t size,
                                  pix_format_t format);

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
 * @brief Run a display's draw callback, assuming the caller has already
 * established it's due.
 *
 * Only if a draw callback is registered (see `pix_display_set_callback()`)
 * and `ops->lock()`/`ops->unlock()` are both implemented and `ops->lock()`
 * actually returns a bitmap, runs the callback and unlocks - a silent no-op
 * otherwise. Doesn't call `ops->poll()` or touch `ts` itself - poll.c's own
 * `pix_poll()` already did both, for every allocated display, before
 * picking @p display as the one due display worth actually drawing this
 * round (see `pix_display_t::ts`'s own doc). Not public API, and not
 * safety-checked against @p display going invalid concurrently - the
 * caller must already hold it claimed (see `pix_display_t::polling`).
 * @param display The display to draw. Must be valid and due.
 */
void _pix_display_draw(pix_display_t *display);

/**
 * @brief Direct-memory pixel get/set, one pair per pix_format_t - see
 * src/picofuse/pix/{rgba32,rgb888,rgb565,mono}.c. Only ever called by
 * bitmap.c's own pix_bitmap_get_pixel()/pix_bitmap_set_pixel(), once
 * @p bitmap is already known in-bounds and `bitmap->ops` has been checked
 * and ruled out - not public API, and not bounds-checked here.
 */
pix_color_t _pix_bitmap_rgba32_get_pixel(const pix_bitmap_t *bitmap,
                                         pix_point_t point);
void _pix_bitmap_rgba32_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                  pix_color_t color);
pix_color_t _pix_bitmap_rgb888_get_pixel(const pix_bitmap_t *bitmap,
                                         pix_point_t point);
void _pix_bitmap_rgb888_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                  pix_color_t color);
pix_color_t _pix_bitmap_rgb565_get_pixel(const pix_bitmap_t *bitmap,
                                         pix_point_t point);
void _pix_bitmap_rgb565_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                  pix_color_t color);
pix_color_t _pix_bitmap_mono_get_pixel(const pix_bitmap_t *bitmap,
                                       pix_point_t point);
void _pix_bitmap_mono_set_pixel(pix_bitmap_t *bitmap, pix_point_t point,
                                pix_color_t color);

/**
 * @brief Direct-memory rect fill, one per pix_format_t - see
 * src/picofuse/pix/{rgba32,rgb888,rgb565,mono}.c. Only ever called by
 * bitmap.c's own pix_bitmap_fill_rect(), once @p origin/@p size are already
 * clipped to @p bitmap's own bounds and `bitmap->ops`/`bitmap->data` have
 * both been checked - not public API, and not bounds-checked here. Free to
 * use a bulk write (`memset()`, say) wherever the format allows it, unlike
 * the get/set_pixel pair above.
 */
void _pix_bitmap_rgba32_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                  pix_size_t size, pix_color_t color);
void _pix_bitmap_rgb888_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                  pix_size_t size, pix_color_t color);
void _pix_bitmap_rgb565_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                  pix_size_t size, pix_color_t color);
void _pix_bitmap_mono_fill_rect(pix_bitmap_t *bitmap, pix_point_t origin,
                                pix_size_t size, pix_color_t color);
