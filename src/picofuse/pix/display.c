#include "private.h"
#include <picofuse/sys/debugf.h>
#include <picofuse/sys/timestamp.h>
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
    display->bitmap =
        (pix_bitmap_t){.data = NULL, .size = size, .stride = 0, .fmt = format};
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

void _pix_display_poll(pix_display_t *display) {
  if (!_pix_display_valid(display) || display->ops->poll == NULL) {
    return;
  }
  bool due = display->ops->poll(display);
  if (!due) {
    return;
  }

  // Stamped as soon as the backend reports due
  display->ts = sys_timestamp_ms();

  // If there's no draw callback or the lock/unlock operations aren't
  // implemented, bail early.
  if (display->draw == NULL || display->ops->lock == NULL ||
      display->ops->unlock == NULL) {
    return;
  }

  pix_bitmap_t *bitmap = display->ops->lock(display);
  if (bitmap == NULL) {
    return;
  }

  // Draw and then unlock
  display->draw(display, bitmap, display->draw_userdata);
  display->ops->unlock(display);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

void pix_display_set_callback(pix_display_t *display,
                              pix_display_draw_t callback, void *userdata) {
  if (!_pix_display_valid(display)) {
    return;
  }
  display->draw = callback;
  display->draw_userdata = userdata;
}

void pix_display_deinit(pix_display_t *display) {
  if (!_pix_display_valid(display)) {
    return;
  }
  sys_debugf("pix", "pix_display_deinit: deinit display %p", (void *)display);
  if (display->ops->deinit != NULL) {
    display->ops->deinit(display);
  }
  _pix_display_free(display);
}
