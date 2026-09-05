/**
 * @file event.h
 * @brief HID event type definitions.
 * @defgroup HIDEvents Events
 * @ingroup HID
 * @brief HID event types/payloads, the keycode/state definitions they
 * carry (see keycode.h), and the hid_event_queue_*()/hid_event_free()
 * functions that produce and release them.
 * @details
 * Events are received from the `sys_event_queue_t*` that was passed to
 * `hid_init()` when the HID instance was created - pop from it with
 * `sys_event_queue_pop()`/`_try_pop()`/`_timed_pop()` and cast the
 * returned `sys_event_t` back to a `hid_event_t*`. `hid_poll()` and the
 * various backends (gpio/timer/signal/wifi/...) are what push events onto
 * that queue in the first place, via the `hid_event_queue_*()` functions
 * below.
 *
 * Every popped event is owned by whichever consumer popped it, and comes
 * from a fixed-size internal pool (see `HID_EVENT_CAPACITY`), not the heap
 * - so it must be released with `hid_event_free()` once handled, or that
 * pool slot is leaked for the remaining lifetime of the HID instance. For
 * a one-shot `hid_register_timer()` device specifically, freeing its
 * event is also what finally deregisters the device - see
 * `hid_event_free()`'s own doc.
 */
#pragma once
#include "device.h"
#include "keycode.h"
#include <picofuse/hw/wifi.h>
#include <picofuse/pix/types.h>
#include <picofuse/sys/io.h>
#include <stdbool.h>

/**
 * @brief Maximum number of in-flight HID events tracked by one HID
 * instance.
 * @ingroup HIDEvents
 *
 * An event occupies a pool slot from the point it's queued (by a
 * hid_event_queue_*() helper) until the consumer releases it with
 * hid_event_free(). Override at compile time, for example:
 * `-DHID_EVENT_CAPACITY=64`.
 */
#ifndef HID_EVENT_CAPACITY
#define HID_EVENT_CAPACITY 16u
#endif

/**
 * @brief HID event payload selector.
 * @ingroup HIDEvents
 */
typedef enum {
  hid_event_type_none = 0,
  hid_event_type_keycode = 1,
  hid_event_type_touch = 2,
  hid_event_type_metric = 3,
  hid_event_type_timer = 4,
  hid_event_type_signal = 5,
  hid_event_type_wifi = 6,
  hid_event_type_iostream = 7,
} hid_event_type_t;

/**
 * @brief Keycode-oriented HID event payload.
 * @ingroup HIDEvents
 */
typedef struct {
  hid_state_t state; ///< HID state flags for this event.
  uint16_t keycode;  ///< HID keycode associated with the event.
} hid_keycode_t;

/**
 * @brief Touch-oriented HID event payload.
 * @ingroup HIDEvents
 */
typedef struct {
  hid_state_t state; ///< HID state flags for this touch event.
  pix_point_t point; ///< Touch/event coordinates in pixels.
  uint8_t slot;      ///< Touch slot index for multi-touch tracking.
} hid_touch_t;

/**
 * @brief Metric-oriented HID event payload.
 * @ingroup HIDEvents
 */
typedef struct {
  const char *name; ///< Metric name.
  const char *unit; ///< Metric unit string.
  float value;      ///< Metric value.
} hid_metric_t;

/**
 * @brief Timer-oriented HID event payload.
 * @ingroup HIDEvents
 */
typedef struct {
  void *userdata; ///< Timer userdata associated with this event.
} hid_timer_t;

/**
 * @brief Signal-oriented HID event payload.
 * @ingroup HIDEvents
 */
typedef struct {
  sys_env_signal_t signal; ///< Environment signal captured by HID source.
} hid_signal_t;

/**
 * @brief Wi-Fi-oriented HID event payload.
 * @ingroup HIDEvents
 *
 * @p network is passed through from the underlying hw_wifi_callback_t
 * without copying: it always points to program-lifetime storage (either
 * a field on the singleton hw_wifi_t handle, or a static buffer reused
 * per scan result — never a freed/stack-transient pointer), so it is
 * never dangling. It may however show newer data than when this event
 * was queued if a later Wi-Fi event arrives before this one is consumed
 * (the same characteristic hw_wifi_callback_t already has). NULL when
 * not applicable (a plain disconnect, or the scan-complete marker).
 */
typedef struct {
  hw_wifi_event_t event; ///< Wi-Fi status event (see hw_wifi_event_t).
  const hw_wifi_network_t *network; ///< Associated network info, or NULL.
} hid_wifi_t;

/**
 * @brief Stream-readiness-oriented HID event payload.
 * @ingroup HIDEvents
 */
typedef struct {
  sys_iostream_event_t events; ///< Readiness event(s) that occurred - see
                               ///< sys_iostream_event_t; may combine
                               ///< sys_iostream_event_read and
                               ///< sys_iostream_event_write.
} hid_iostream_t;

/**
 * @brief Represents a single HID input event.
 * @ingroup HIDEvents
 */
typedef struct {
  hid_device_t *device;  ///< HID child-device associated with this event.
  hid_event_type_t type; ///< Event payload tag.
  union {
    hid_keycode_t keycode;
    hid_touch_t touch;
    hid_metric_t metric;
    hid_timer_t timer;
    hid_signal_t signal;
    hid_wifi_t wifi;
    hid_iostream_t iostream;
  } data; ///< Payload selected by type.
} hid_event_t;

/**
 * @brief Queue a keycode-based HID event to the owning HID instance queue.
 * @ingroup HIDEvents
 * @param device HID device associated with the event.
 * @param state Input transition flags to apply (for example
 * hid_state_on/hid_state_off).
 * @param keycode HID keycode associated with the event.
 * @retval true Event queued successfully.
 * @retval false Queueing failed.
 *
 * The helper applies @p state to the device-local HID state, then queues an
 * event containing the resulting state snapshot.
 */
bool hid_event_queue_keycode(hid_device_t *device, hid_state_t state,
                             uint16_t keycode);

/**
 * @brief Queue a float metric HID event to a HID instance queue.
 * @ingroup HIDEvents
 * @param device HID device associated with the destination queue.
 * @param name Metric name string.
 * @param unit Metric unit string.
 * @param value Metric value.
 * @retval true Event queued successfully.
 * @retval false Queueing failed.
 */
bool hid_event_queue_metric_float(hid_device_t *device, const char *name,
                                  const char *unit, float value);

/**
 * @brief Queue a touch HID event to the owning HID instance queue.
 * @ingroup HIDEvents
 * @param device HID device associated with the event.
 * @param state Touch state flags for this event.
 * @param point Touch coordinates in pixels.
 * @param slot Touch slot index.
 * @retval true Event queued successfully.
 * @retval false Queueing failed.
 */
bool hid_event_queue_touch(hid_device_t *device, hid_state_t state,
                           pix_point_t point, uint8_t slot);

/**
 * @brief Queue a signal HID event to the owning HID instance queue.
 * @ingroup HIDEvents
 * @param device HID device associated with the event.
 * @param signal Environment signal value to publish.
 * @retval true Event queued successfully.
 * @retval false Queueing failed.
 */
bool hid_event_queue_signal(hid_device_t *device, sys_env_signal_t signal);

/**
 * @brief Queue a Wi-Fi status HID event to the owning HID instance queue.
 * @ingroup HIDEvents
 * @param device HID device associated with the event.
 * @param event Wi-Fi status event to publish (see hw_wifi_event_t).
 * @param network Associated network info, or NULL if not applicable. Not
 * copied — see hid_wifi_t for the pointer's lifetime characteristics.
 * @retval true Event queued successfully.
 * @retval false Queueing failed.
 */
bool hid_event_queue_wifi(hid_device_t *device, hw_wifi_event_t event,
                          const hw_wifi_network_t *network);

/**
 * @brief Queue a stream-readiness HID event to the owning HID instance
 * queue.
 * @ingroup HIDEvents
 * @param device HID device associated with the event.
 * @param events Readiness event(s) to publish - see sys_iostream_event_t.
 * @retval true Event queued successfully.
 * @retval false Queueing failed.
 */
bool hid_event_queue_iostream(hid_device_t *device,
                              sys_iostream_event_t events);

/**
 * @brief Free a HID event allocated internally by HID queue helpers.
 * @ingroup HIDEvents
 * @param event Event pointer to release.
 *
 * For a one-shot (non-repeating) hid_register_timer() device, freeing its
 * `hid_event_type_timer` event is also what finally deregisters the
 * device - the timer itself already stopped when the event fired, but the
 * hid_device_t pool slot is kept alive until this call, since @p event's
 * `device` field must stay valid until the consumer is done with the
 * event. No separate hid_deregister() call is needed for a one-shot
 * timer; the device is already gone once this returns.
 */
void hid_event_free(hid_event_t *event);
