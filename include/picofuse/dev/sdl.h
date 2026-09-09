/**
 * @file sdl.h
 * @brief SDL display interface.
 * @defgroup SDL SDL
 * @ingroup Display
 *
 * This module provides a device-level API for SDL-based displays.
 */
#pragma once
#include <picofuse/pix.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef enum {
  dev_sdl_none = 0,
  dev_sdl_fullscreen = 1 << 0,
  dev_sdl_borderless = 1 << 1,
  dev_sdl_centered = 1 << 2,  ///< Center the window - the default position
                              ///< is otherwise platform-defined.
  dev_sdl_hidden = 1 << 3,    ///< Create the window without showing it -
                              ///< for exercising this backend headlessly
                              ///< (CI, no display server) with a real
                              ///< window/event loop behind it.
} dev_sdl_flags_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

pix_display_t *dev_sdl_init(const char *title, pix_size_t size,
                            pix_format_t format, dev_sdl_flags_t flags,
                            uint16_t interval_ms);

/** @} */
