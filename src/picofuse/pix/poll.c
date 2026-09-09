#include "private.h"
#include <stddef.h>

#ifndef SYSTEM_NAME_PICO
#include <pthread.h>
// See private.h's own extern declaration - this is the one definition
// every translation unit that includes it (display.c too) links against.
pthread_mutex_t _pix_display_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Finds the allocated display with the smallest `ts`, or NULL if
 * none are allocated.
 *
 * PIX_DISPLAY_POOL_CAPACITY is small (a handful of displays at most), so a
 * single locked linear scan is already the efficient answer here - cheaper,
 * in both time and code, than keeping some sorted structure in sync across
 * every alloc/free/unlock for a pool this size. */
static pix_display_t *_pix_poll_next(void) {
  _PIX_DISPLAY_LOCK();
  pix_display_t *next = NULL;
  for (size_t i = 0; i < PIX_DISPLAY_POOL_CAPACITY; i++) {
    pix_display_t *display = &_pix_display_pool[i];
    if (display->ops == NULL) {
      continue;
    }
    if (next == NULL || display->ts < next->ts) {
      next = display;
    }
  }
  _PIX_DISPLAY_UNLOCK();
  return next;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

bool pix_poll(void) {
  pix_display_t *display = _pix_poll_next();
  if (display == NULL) {
    return false;
  }
  _pix_display_poll(display);
  return true;
}
