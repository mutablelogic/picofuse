/**
 * @file hid.h
 * @brief Aggregates HID component interfaces.
 * @defgroup HID Human Interface Device (HID)
 * @ingroup Picofuse
 * @details
 * The HID module provides a unified event-oriented interface for input and
 * sensor-like producers, including GPIO-backed keys, timer sources, touch
 * controllers, signal notifications, and device-specific integrations.
 *
 * A HID instance is created with `hid_init()` and bound to a system event
 * queue. Backends register one or more HID devices under that instance,
 * either through generic registration (`hid_register`) or convenience helpers
 * such as timer and GPIO registration functions.
 *
 * During runtime, applications call `hid_poll()` to let registered backends
 * produce events. Produced events are queued as `hid_event_t` values and
 * consumed by the application event loop. The event payload union carries the
 * event-specific data for keycodes, timers, touch coordinates, metrics, and
 * signals.
 *
 * Typical flow:
 * 1. Create a queue and initialize HID with `hid_init()`.
 * 2. Register one or more HID devices/sources.
 * 3. Call `hid_poll()` regularly (or from a runloop poll callback).
 * 4. Handle queued `hid_event_t` events and release each with
 *    `hid_event_free()`.
 * 5. On shutdown, `hid_deinit()` will clean up devices and resources.
 */
#pragma once
#include "hid/device.h"
#include "hid/event.h"
#include "hid/keycode.h"
