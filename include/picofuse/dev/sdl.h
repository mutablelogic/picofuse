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

/**
 * @brief Flags for SDL display creation.
 * @ingroup SDL
 */
typedef enum {
  dev_sdl_none = 0,             ///< No flags.
  dev_sdl_fullscreen = 1 << 0,  ///< Open the window fullscreen.
  dev_sdl_borderless = 1 << 1,  ///< Create the window without a title bar
                                ///< or borders.
  dev_sdl_centered = 1 << 2,    ///< Center the window - the default
                                ///< position is otherwise platform-defined.
  dev_sdl_hidden = 1 << 3,      ///< Create the window without showing it -
                                ///< for exercising this backend headlessly
                                ///< (CI, no display server) with a real
                                ///< window/event loop behind it.
} dev_sdl_flags_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize an SDL-backed display.
 * @ingroup SDL
 * @param title The window title. `NULL` falls back to the program's own
 * name.
 * @param size The display size, in pixels.
 * @param format The pixel format the display's bitmap should use.
 * @param flags Window creation flags - see @ref dev_sdl_flags_t.
 * @param interval_ms Minimum time between flushes, in ms - `0` for no rate
 * limit ("as often as possible").
 * @return A display handle bound to a real SDL window, or `NULL` on
 * failure (SDL2 unavailable on this build, an unsupported @p format, or
 * the underlying `SDL_CreateWindow()`/`SDL_CreateRenderer()`/
 * `SDL_CreateTexture()` call failing).
 */
pix_display_t *dev_sdl_init(const char *title, pix_size_t size,
                            pix_format_t format, dev_sdl_flags_t flags,
                            uint16_t interval_ms);

/** @} */
