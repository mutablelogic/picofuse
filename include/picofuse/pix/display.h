/**
 * @file display.h
 * @brief Opaque display handle.
 * @defgroup PixelDisplay Display
 * @ingroup Pixel
 *
 * A display is a backend-agnostic handle onto a real screen - dev/sdl.h
 * (and, in future, other display backends) is what actually creates one.
 * The vtable a backend implements to become one is private - see
 * `src/picofuse/pix/private.h` - since it's only ever needed by the
 * backend itself and by the display pool, never by a public API caller.
 */
#pragma once
#include "bitmap.h"
#include <stdbool.h>

/**
 * @brief Opaque display descriptor.
 * @ingroup PixelDisplay
 */
typedef struct pix_display_t pix_display_t;

/**
 * @brief Maximum number of concurrently active displays.
 * @ingroup PixelDisplay
 *
 * Displays come from a small fixed-size pool, not the heap - see
 * `src/picofuse/pix/private.h`. Override by defining
 * `PIX_DISPLAY_POOL_CAPACITY` at compile time.
 */
#ifndef PIX_DISPLAY_POOL_CAPACITY
#define PIX_DISPLAY_POOL_CAPACITY 1u
#endif

/**
 * @brief Size in bytes of backend-private state embedded in each display.
 * @ingroup PixelDisplay
 *
 * Sized for dev/sdl.h's own context (window/renderer/texture pointers plus
 * bookkeeping - see src/picofuse/dev/sdl/sdl.c's `_dev_sdl_ctx_t`), the
 * largest backend today. Must be defined identically for every translation
 * unit that touches `pix_display_t` - it sizes a fixed embedded buffer, not
 * a per-backend allocation, so a per-target override here would silently
 * disagree with `picofuse-pix`'s own build of the same struct. Override by
 * defining `PIX_DISPLAY_CONTEXT_SIZE` globally at compile time instead.
 */
#ifndef PIX_DISPLAY_CONTEXT_SIZE
#define PIX_DISPLAY_CONTEXT_SIZE 64u
#endif

/**
 * @brief Draw callback signature.
 * @ingroup PixelDisplay
 * @param display The display being drawn.
 * @param bitmap The display's bitmap, already locked for direct pixel
 * access - the callback should draw into it, but never lock/unlock it
 * itself.
 * @param userdata Opaque pointer, as passed to `pix_display_set_callback()`.
 *
 * Called internally, from within `pix_poll()`, whenever this display's
 * backend reports it's due a redraw - never called directly.
 */
typedef void (*pix_display_draw_t)(pix_display_t *display, pix_bitmap_t *bitmap,
                                   void *userdata);

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Register the function that draws this display's contents.
 * @ingroup PixelDisplay
 * @param display The display to set the callback on. A no-op if invalid.
 * @param callback Called whenever @p display is due a redraw, or `NULL` to
 * clear a previously registered callback.
 * @param userdata Opaque pointer passed back to @p callback unchanged.
 */
void pix_display_set_callback(pix_display_t *display,
                              pix_display_draw_t callback, void *userdata);

/**
 * @brief Deinitialize a display and release it back to the pool.
 * @ingroup PixelDisplay
 * @param display The display to deinitialize. Safe to call on NULL.
 */
void pix_display_deinit(pix_display_t *display);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// SCHEDULING

/** @name Scheduling
 * @{ */

/**
 * @brief Service whichever allocated display needs updating.
 * @ingroup PixelDisplay
 * @return `true` if a display is found and polled.
 *
 * Call this from the application's own main loop. It picks the next
 * display which needs updated, polls it, and - if its backend reports it's
 * due a redraw and a draw callback is registered (see
 * `pix_display_set_callback()`) - locks it, runs the callback, and unlocks
 * it again.
 */
bool pix_poll(void);

/** @} */
