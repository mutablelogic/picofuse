#include "private.h"
#include <picofuse/sys/timestamp.h>
#include <stddef.h>

#ifndef SYSTEM_NAME_PICO
#include <pthread.h>
pthread_mutex_t _pix_display_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Tries to claim @p display (see pix_display_t::polling's own doc)
 * for an in-flight poll - `false` if it isn't allocated, or is already
 * claimed by a concurrent pix_poll() on another thread/core. */
static bool _pix_poll_claim(pix_display_t *display) {
  _PIX_DISPLAY_LOCK();
  bool claimed = display->ops != NULL && sys_atomic_get(&display->polling) == 0;
  if (claimed) {
    sys_atomic_set(&display->polling, 1);
  }
  _PIX_DISPLAY_UNLOCK();
  return claimed;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * PIX_DISPLAY_POOL_CAPACITY is small (a handful of displays at most), so a
 * single linear scan of the whole pool, every call, is already the
 * efficient answer here - cheaper, in both time and code, than keeping
 * some sorted structure in sync across every alloc/free/unlock for a pool
 * this size.
 *
 * Every allocated display gets ops->poll() called on it this round, not
 * just whichever one ends up drawn - see pix_display_ops_t::poll's own
 * doc on why (event pumping). Of however many report due in the same
 * pass, only the fairest (smallest `ts`, i.e. the one that's gone longest
 * since it was last actually drawn) gets _pix_display_draw()'d - see
 * pix_display_t::ts's own doc on why that's what keeps multiple due
 * displays with different intervals taking fair turns, rather than a
 * long-interval one's stale `ts` letting it starve a short-interval one
 * that's genuinely due more often.
 */
bool pix_poll(void) {
  pix_display_t *candidate = NULL;
  uint64_t candidate_ts = 0;

  for (size_t i = 0; i < PIX_DISPLAY_POOL_CAPACITY; i++) {
    pix_display_t *display = &_pix_display_pool[i];
    if (!_pix_poll_claim(display)) {
      continue;
    }

    uint64_t ts_before = display->ts;
    bool due = display->ops->poll != NULL && display->ops->poll(display);
    if (!due || (candidate != NULL && ts_before >= candidate_ts)) {
      sys_atomic_set(&display->polling, 0);
      continue;
    }

    // The fairest due display seen so far this round - release whichever
    // one previously held that title (if any), keep this one claimed
    // until the scan finishes.
    if (candidate != NULL) {
      sys_atomic_set(&candidate->polling, 0);
    }
    candidate = display;
    candidate_ts = ts_before;
  }

  if (candidate == NULL) {
    return false;
  }

  // Only the winner's `ts` advances - see pix_display_t::ts's own doc.
  candidate->ts = sys_timestamp_ms();
  _pix_display_draw(candidate);
  sys_atomic_set(&candidate->polling, 0);
  return true;
}
