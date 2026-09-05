/**
 * @file infrared.h
 * @brief Infrared (IR) receiver and transmitter interface
 * @defgroup Infrared Infrared
 * @ingroup Hardware
 *
 * Infrared interface for capturing and generating the raw MARK/SPACE
 * timing that consumer IR remotes use - not any particular protocol.
 * NEC, RC5, SIRC, and so on are all just a specific sequence of MARK
 * (carrier on) and SPACE (carrier off) durations; decoding or encoding
 * a specific protocol from/to that sequence is a separate "codec" layer
 * built on top of this module (see hw_infrared_set_callback()), not
 * something this header knows about.
 *
 * A single hw_infrared_t can receive, transmit, or both, since a remote
 * receiver and an IR LED are physically independent hardware - pass
 * NULL for whichever side (pin or device) you don't want.
 */
#pragma once
#include "gpio.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @def HW_INFRARED_CAPACITY
 * @ingroup Infrared
 * @brief Maximum number of simultaneously open Infrared instances. One
  instance can be either a receiver, a transmitter, or both.
 */
#ifndef HW_INFRARED_CAPACITY
#define HW_INFRARED_CAPACITY 1
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Infrared receiver/transmitter handle.
 * @ingroup Infrared
 * @headerfile infrared.h picofuse/hw.h
 */
typedef struct hw_infrared_t hw_infrared_t;

/**
 * @brief Infrared receive event types.
 * @ingroup Infrared
 */
typedef enum {
  hw_infrared_event_mark,    ///< The carrier was on for duration_us.
  hw_infrared_event_space,   ///< The carrier was off for duration_us.
  hw_infrared_event_timeout, ///< duration_us exceeded hw_infrared_config_t's
                             ///< timeout_us - treat any in-progress frame as
                             ///< abandoned and start decoding fresh.
} hw_infrared_event_t;

/**
 * @brief Infrared receive callback.
 * @ingroup Infrared
 * @param ir The handle the event occurred on.
 * @param event Which kind of event this is.
 * @param duration_us How long the mark or space lasted, or how long the
 * receiver had been idle when a timeout was declared.
 * @param userdata User-defined data pointer provided to
 * hw_infrared_set_callback().
 */
typedef void (*hw_infrared_callback_t)(hw_infrared_t *ir,
                                       hw_infrared_event_t event,
                                       uint32_t duration_us, void *userdata);

/**
 * @brief Infrared initialization configuration.
 * @ingroup Infrared
 *
 * When NULL is passed to hw_infrared_init()/hw_infrared_init_device(),
 * every field here defaults as documented.
 */
typedef struct {
  /**
   * TX carrier frequency in Hz. 0 uses the default, 38000 (38kHz) - the
   * most common consumer IR carrier. Ignored if tx_pin/tx_device is
   * NULL.
   */
  uint32_t carrier_freq;
  /**
   * RX duration, in microseconds, above which an event is reported as
   * hw_infrared_event_timeout instead of hw_infrared_event_space. 0
   * uses the default, 50000 (50ms) - comfortably longer than the
   * inter-frame gap of any common protocol's individual frame, but
   * short enough to promptly notice a remote has stopped transmitting.
   * Ignored if rx_pin/rx_device is NULL.
   */
  uint32_t timeout_us;
} hw_infrared_config_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize an Infrared receiver and/or transmitter.
 * @ingroup Infrared
 * @param rx_pin GPIO pin wired to an IR receiver module's output (e.g. a
 * TSOP38238), or NULL to not enable receiving.
 * @param tx_pin GPIO pin wired to an IR LED (directly or via a driver
 * transistor), or NULL to not enable transmitting.
 * @param config Optional pointer to extended configuration. Pass NULL to
 * use default carrier frequency and receive timeout.
 * @return An initialized handle, or NULL if both @p rx_pin and @p tx_pin
 * are NULL, initialization fails, or the receiver/transmitter pool is
 * exhausted. Release it with hw_infrared_deinit().
 *
 * Receive events are not reported anywhere until a callback is attached
 * with hw_infrared_set_callback() - that's a separate step so that a
 * protocol codec can own the callback without hw_infrared_init() itself
 * needing to know codecs exist.
 */
hw_infrared_t *hw_infrared_init(const hw_gpio_t *rx_pin,
                                const hw_gpio_t *tx_pin,
                                const hw_infrared_config_t *config);

/**
 * @brief Initialize an Infrared receiver and/or transmitter by device path.
 * @ingroup Infrared
 * @param rx_device Device path for a real IR receiver, or NULL to not
 * enable receiving.
 * @param tx_device Device path for a real IR transmitter, or NULL to not
 * enable transmitting.
 * @param config Optional pointer to extended configuration. Pass NULL to
 * use default carrier frequency and receive timeout.
 * @return An initialized handle, or NULL if both @p rx_device and
 * @p tx_device are NULL, initialization fails, or the receiver/
 * transmitter pool is exhausted. Release it with hw_infrared_deinit().
 *
 * Host platforms have no GPIO pins for this - hw_infrared_init() is
 * Pico-only, mirroring hw_uart_init()/hw_uart_init_device()'s own split.
 */
hw_infrared_t *hw_infrared_init_device(const char *rx_device,
                                       const char *tx_device,
                                       const hw_infrared_config_t *config);

/**
 * @brief Deinitialize and release an Infrared handle.
 * @ingroup Infrared
 * @param ir Pointer to the handle to deinitialize, or NULL (a no-op).
 */
void hw_infrared_deinit(hw_infrared_t *ir);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Set or clear the receive callback.
 * @ingroup Infrared
 * @param ir The handle to observe.
 * @param callback Callback to invoke on each receive event, or NULL to
 * remove the current callback.
 * @param userdata User-defined data pointer passed to @p callback.
 * @return true if the callback was registered, false if @p ir is NULL or
 * has no receiver configured.
 */
bool hw_infrared_set_callback(hw_infrared_t *ir,
                              hw_infrared_callback_t callback, void *userdata);

/**
 * @brief Transmit a sequence of IR mark/space durations.
 * @ingroup Infrared
 * @param ir The handle to transmit on.
 * @param durations_us Alternating mark/space durations in microseconds,
 * starting with a mark - the same raw representation LIRC uses, and what
 * a protocol codec builds from a decoded button press.
 * @param count Number of entries in @p durations_us.
 * @return true if the whole sequence was transmitted, false if @p ir is
 * NULL, has no transmitter configured, or @p durations_us is NULL with a
 * nonzero @p count.
 *
 * Blocks until transmission completes - a full frame takes anywhere from
 * a few milliseconds to a few tens of milliseconds, comparable to
 * hw_led_neopixel's own blocking flush, and asynchronous transmission
 * isn't something callers have needed so far.
 */
bool hw_infrared_transmit(hw_infrared_t *ir, const uint32_t *durations_us,
                          size_t count);

/** @} */
