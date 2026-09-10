#include "private.h"
#include <picofuse/sys/debugf.h>
#include <picofuse/sys/sleep.h>
#include <stddef.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

// Not static - poll.c's own pix_poll() iterates this too (see private.h's
// own extern declaration).
pix_display_t _pix_display_pool[PIX_DISPLAY_POOL_CAPACITY] = {0};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static inline bool _pix_display_valid(const pix_display_t *display) {
  return display != NULL && display->ops != NULL;
}

/** @brief Claims a free slot from the static instance pool, or NULL if
 * every slot is already in use. */
static inline pix_display_t *_pix_display_pool_claim(void) {
  for (size_t i = 0; i < PIX_DISPLAY_POOL_CAPACITY; i++) {
    if (_pix_display_pool[i].ops == NULL) {
      return &_pix_display_pool[i];
    }
  }
  return NULL;
}

pix_display_t *_pix_display_alloc(const pix_display_ops_t *ops, pix_size_t size,
                                  pix_format_t format) {
  if (ops == NULL) {
    return NULL;
  }

  _PIX_DISPLAY_LOCK();
  pix_display_t *display = _pix_display_pool_claim();
  if (display != NULL) {
    memset(display->context, 0, sizeof(display->context));
    // Seed the dirty rect to the whole display, so it's due a flush right
    // away - see _pix_display_alloc()'s own doc.
    display->dirty_origin = (pix_point_t){0};
    display->dirty_size = size;
    display->ts = 0;
    display->draw = NULL;
    display->draw_userdata = NULL;
    display->bitmap = (pix_bitmap_t){.data = NULL,
                                     .size = size,
                                     .stride = 0,
                                     .fmt = format,
                                     .op = PIX_BLEND};
    sys_atomic_init(&display->polling, 0);
    display->ops = ops;
  }
  _PIX_DISPLAY_UNLOCK();
  return display;
}

void *_pix_display_context(const pix_display_t *display) {
  return _pix_display_valid(display) ? (void *)display->context : NULL;
}

void _pix_display_free(pix_display_t *display) {
  if (!_pix_display_valid(display)) {
    return;
  }
  _PIX_DISPLAY_LOCK();
  display->ops = NULL;
  _PIX_DISPLAY_UNLOCK();
}

void _pix_display_draw(pix_display_t *display) {
  // Snapshot together, under the same lock pix_display_set_callback() uses
  // to write them - otherwise a concurrent call to it could change `draw`
  // (or clear it to NULL) between the check below and the call further
  // down, or pair a new `draw` with a stale `draw_userdata`.
  _PIX_DISPLAY_LOCK();
  pix_display_draw_t draw = display->draw;
  void *draw_userdata = display->draw_userdata;
  _PIX_DISPLAY_UNLOCK();

  // If there's no draw callback or the lock/unlock operations aren't
  // implemented, bail early.
  if (draw == NULL || display->ops->lock == NULL ||
      display->ops->unlock == NULL) {
    return;
  }

  pix_bitmap_t *bitmap = display->ops->lock(display);
  if (bitmap == NULL) {
    return;
  }

  // Draw and then unlock
  draw(display, bitmap, draw_userdata);
  display->ops->unlock(display);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void pix_display_set_callback(pix_display_t *display,
                              pix_display_draw_t callback, void *userdata) {
  if (!_pix_display_valid(display)) {
    return;
  }
  _PIX_DISPLAY_LOCK();
  display->draw = callback;
  display->draw_userdata = userdata;
  _PIX_DISPLAY_UNLOCK();
}

void pix_display_deinit(pix_display_t *display) {
  if (!_pix_display_valid(display)) {
    return;
  }

  // Claim the display for teardown the same way pix_poll()'s own
  // _pix_poll_claim() claims it for a poll round - observing `polling == 0`
  // and setting it back to 1 have to happen as one step under the pool
  // lock, not as a separate unlocked check-then-act, otherwise a
  // pix_poll() on another thread/core could slip in and claim the display
  // for itself in the gap between this function seeing `polling` hit 0 and
  // actually calling ops->deinit(), racing teardown against an in-flight
  // poll/draw. Held through _pix_display_free() below, so no pix_poll()
  // can claim this display at all from here on.
  for (;;) {
    _PIX_DISPLAY_LOCK();
    if (sys_atomic_get(&display->polling) == 0) {
      sys_atomic_set(&display->polling, 1);
      _PIX_DISPLAY_UNLOCK();
      break;
    }
    _PIX_DISPLAY_UNLOCK();
    sys_sleep_ms(1);
  }

  sys_debugf("pix", "pix_display_deinit: deinit display %p", (void *)display);
  if (display->ops->deinit != NULL) {
    display->ops->deinit(display);
  }
  _pix_display_free(display);
  sys_atomic_set(&display->polling, 0);
}
