/**
 * @file usb.h
 * @brief USB host interface
 * @defgroup USB USB
 * @ingroup Hardware
 *
 * USB host interface for hardware platforms.
 *
 * This module provides USB host functionality, including detection of devices
 * as they are attached and detached. @ref hw_usb_init brings the host
 * controller up but attaches no callback - use @ref hw_usb_set_callback for
 * that, separately, so that whichever part of a program brings USB up
 * doesn't have to be the same part that observes it (see
 * @ref hw_usb_register_hid for exactly that: a HID bridge that only
 * observes a handle some other part of the program owns). Once a callback
 * is attached, it's fired once for each device already connected to the
 * host, with the @ref hw_usb_event_attached event, then again whenever a
 * device is physically attached or detached from then on.
 *
 * Enumeration happens at the *interface* level, not the device level: a
 * composite device (a keyboard+mouse combo, any multi-function gadget)
 * fires the callback once per interface it exposes, not once for the whole
 * device - the same shape the host OS itself typically uses (one
 * `/dev/input/eventN` per interface on Linux, for example). Every one of
 * those events shares the same @ref hw_usb_device_t::device_id,
 * @ref hw_usb_device_t::vid, @ref hw_usb_device_t::pid and string fields,
 * but carries that interface's own @ref hw_usb_device_t::interface_number
 * and interface class/subclass/protocol - which is where the *useful*
 * classification actually lives for a composite device, since its
 * device-level class is conventionally `0x00` ("per-interface",
 * @ref hw_usb_device_class_per_interface) and tells you nothing on its
 * own. Only interface alternate setting 0 (the default/active one) is
 * considered.
 *
 * The @ref hw_usb_device_t structure describes one interface of a connected
 * device. It carries the USB vendor/product identifiers, the device and
 * interface class metadata, and the string descriptors that are available
 * from the backend. On detach, the @p manufacturer, @p product and @p
 * serial string fields may be empty, and some backends may only be able to
 * provide zeroed identifier fields on a fallback detach path. If interface
 * information couldn't be determined (the configuration descriptor wasn't
 * readable, or declared no interfaces), a single fallback event fires with
 * @ref hw_usb_device_t::interface_number set to `0xFF` and the interface
 * class/subclass/protocol fields copied from the device-level ones.
 *
 * Class-specific functionality (HID input, CDC-ACM serial streams, mass
 * storage) is handled by separate modules that consume the device information
 * provided here.
 *
 * @todo No module actually reads HID *input* (keystrokes, mouse movement)
 * yet - something like `hw_usb_register_hid_device()` (working name only;
 * needs a better one, and a real signature - presumably taking a
 * `hw_usb_device_t` identifying which interface to read) for keyboard and
 * mouse to start with, scoped to boot-protocol interfaces (@ref
 * hw_usb_device_subclass_boot_interface, `interface_protocol` ==
 * @ref hw_usb_device_protocol_keyboard / @ref hw_usb_device_protocol_mouse)
 * so a fixed, known report shape (8 bytes keyboard, 3-4 bytes mouse) can be
 * assumed without a general HID report-descriptor parser. This is NOT a
 * single cross-platform implementation on top of this module - the three
 * platforms need three genuinely different backends:
 *   - Pico: safe to do directly through TinyUSB's own HID class driver
 *     (`CFG_TUH_HID`, currently left at 0 in `tusb_config.h`, plus
 *     `tuh_hid_report_received_cb()`) - this process's USB stack is the
 *     only consumer of the bus, so there's nothing else to conflict with.
 *   - Linux: NOT through libusb/this module at all - the kernel's own
 *     `usbhid` driver already owns the interface the instant it's plugged
 *     in (confirmed via `lsusb -t` showing `Driver=usbhid`), so reading it
 *     via libusb would need `libusb_detach_kernel_driver()`, which steals
 *     the device from the rest of the running system (e.g. the real
 *     keyboard stops working for the OS itself). The correct mechanism is
 *     evdev (`/dev/input/eventN`), read alongside the OS rather than
 *     instead of it - see the already-reserved but unimplemented
 *     @ref hid_type_evdev.
 *   - Darwin: same reasoning as Linux, different API - `IOHIDManager`/
 *     `IOHIDDeviceClient` (IOKit's HID Manager) taps into HID collections
 *     the kernel's own driver already parsed, without taking exclusive
 *     ownership. Needs the user to grant Input Monitoring permission on
 *     modern macOS.
 * The Linux/Darwin backends don't need @ref hw_usb_init/`PICOFUSE_USB`
 * engaged at all, since they observe at the kernel-input layer rather than
 * the raw USB layer this module provides.
 *
 * On the Pico platform, the USB peripheral is fixed hardware and operates in
 * host mode exclusively. On Linux and macOS, the host controller is managed
 * via libusb. In both cases, @ref hw_usb_init takes no platform-specific
 * address parameter.
 *
 * @note On the Pico (RP2040), the USB peripheral and the UART/debug interface
 * share the same physical USB connector. Enabling USB host mode will prevent
 * the device from appearing as a USB serial device to a connected host PC.
 * Use a debug probe if you need simultaneous debug output.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Forward-declared rather than #include <picofuse/hid/device.h> - only
// used as opaque pointers below (hw_usb_register_hid()'s own parameter/
// return type), and pulling in the whole header would add a hw->hid
// header dependency this module doesn't otherwise have (hid already
// depends on hw, not the other way around - see hw_usb_register_hid()'s
// own doc on why the bridge itself still has to be compiled into
// picofuse-hid rather than picofuse-hw).
typedef struct hid_t hid_t;
typedef struct hid_device_t hid_device_t;

/**
 * @def HW_USB_STRING_MAX_LENGTH
 * @ingroup USB
 * @brief Maximum length of USB string descriptor fields, excluding the null
 * terminator.
 */
#ifndef HW_USB_STRING_MAX_LENGTH
#define HW_USB_STRING_MAX_LENGTH 63
#endif

/**
 * @def HW_USB_INTERFACE_MAX_COUNT
 * @ingroup USB
 * @brief Maximum number of interfaces a backend will report attach/detach
 * events for on any single device - internal to each backend's own
 * per-device interface cache, not a limit on any public array.
 */
#ifndef HW_USB_INTERFACE_MAX_COUNT
#define HW_USB_INTERFACE_MAX_COUNT 8
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief USB hotplug event type.
 * @ingroup USB
 */
typedef enum {
  hw_usb_event_attached = (1 << 0), ///< A device has been attached
  hw_usb_event_detached = (1 << 1), ///< A device has been detached
} hw_usb_event_t;

/**
 * @brief USB device class codes.
 * @ingroup USB
 *
 * These are the standard bDeviceClass descriptor values defined by USB.
 * A value of 0x00 means the class is defined at the interface level.
 */
typedef enum {
  hw_usb_device_class_per_interface = 0x00,
  hw_usb_device_class_audio = 0x01,
  hw_usb_device_class_communications = 0x02,
  hw_usb_device_class_hid = 0x03,
  hw_usb_device_class_physical = 0x05,
  hw_usb_device_class_image = 0x06,
  hw_usb_device_class_printer = 0x07,
  hw_usb_device_class_mass_storage = 0x08,
  hw_usb_device_class_hub = 0x09,
  hw_usb_device_class_cdc_data = 0x0A,
  hw_usb_device_class_smart_card = 0x0B,
  hw_usb_device_class_content_security = 0x0D,
  hw_usb_device_class_video = 0x0E,
  hw_usb_device_class_personal_healthcare = 0x0F,
  hw_usb_device_class_audio_video = 0x10,
  hw_usb_device_class_bluetooth = 0xE0,
  hw_usb_device_class_miscellaneous = 0xEF,
  hw_usb_device_class_application_specific = 0xFE,
  hw_usb_device_class_vendor_specific = 0xFF,
} hw_usb_device_class_t;

/**
 * @brief Convert a USB device class code to a display string.
 * @ingroup USB
 *
 * In debug builds this returns a symbolic name such as
 * "hw_usb_device_class_hid" when known; otherwise it returns a hexadecimal
 * fallback formatted as "0x%02X". In non-debug builds this always returns
 * the hexadecimal fallback.
 *
 * @param device_class USB device class code.
 * @return Pointer to an internal string buffer.
 */
const char *hw_usb_device_class_to_string(hw_usb_device_class_t device_class);

/**
 * @brief USB device subclass codes.
 * @ingroup USB
 *
 * Subclass values are class-specific USB descriptor codes. Only common raw
 * values are named here; callers may still observe any 8-bit descriptor value.
 */
typedef enum {
  hw_usb_device_subclass_none = 0x00,
  hw_usb_device_subclass_boot_interface = 0x01,
  hw_usb_device_subclass_abstract_control_model = 0x02,
  hw_usb_device_subclass_vendor_specific = 0xFF,
} hw_usb_device_subclass_t;

/**
 * @brief USB device protocol codes.
 * @ingroup USB
 *
 * Protocol values are class-specific USB descriptor codes. Only the raw
 * descriptor value is standardized here; callers may still observe any 8-bit
 * value defined by the device's class.
 *
 * @ref hw_usb_device_protocol_keyboard and @ref hw_usb_device_protocol_mouse
 * are only meaningful when the interface's subclass is
 * @ref hw_usb_device_subclass_boot_interface - they're USB HID's own "boot
 * protocol" codes, a simplified report format BIOS/bootloader-level code can
 * read without parsing the device's actual HID report descriptor. Support
 * for it is optional and only covers keyboards and mice: a non-boot HID
 * interface (a keyboard's own media-key/consumer-control interface, a
 * touchpad, a joystick, a gamepad, ...) reports
 * @ref hw_usb_device_protocol_none here regardless of what it actually is -
 * telling those apart needs the interface's HID report descriptor itself
 * (Usage Page/Usage - Generic Desktop's Mouse/Joystick/Gamepad/Keyboard,
 * Digitizers' Touch Pad, ...), which nothing in this module parses.
 */
typedef enum {
  hw_usb_device_protocol_none = 0x00,
  hw_usb_device_protocol_keyboard = 0x01,
  hw_usb_device_protocol_mouse = 0x02,
} hw_usb_device_protocol_t;

/**
 * @brief Convert a USB device protocol code to a display string.
 * @ingroup USB
 *
 * In debug builds this returns a symbolic name such as
 * "hw_usb_device_protocol_keyboard" when known; otherwise it returns a
 * hexadecimal fallback formatted as "0x%02X". In non-debug builds this
 * always returns the hexadecimal fallback.
 *
 * @param device_protocol USB device protocol code.
 * @return Pointer to an internal string buffer.
 */
const char *
hw_usb_device_protocol_to_string(hw_usb_device_protocol_t device_protocol);

/**
 * @brief Describes one interface of a USB device observed by the host.
 * @ingroup USB
 *
 * This structure is populated when an interface is attached or detached -
 * see this file's own top-level doc on why enumeration happens at the
 * interface level, not the device level.
 *
 * @note On detach, @p manufacturer, @p product and @p serial may be empty
 * strings. Callers should not rely on them being populated for
 * @ref hw_usb_event_detached.
 */
typedef struct {
  uint32_t device_id; ///< Opaque identifier, stable across every interface
                      ///< event belonging to one physical attach/detach -
                      ///< disambiguates two simultaneously-attached
                      ///< devices that happen to share the same vid/pid.
                      ///< Not stable across a replug of the same device.
  uint16_t vid;                             ///< USB Vendor ID
  uint16_t pid;                             ///< USB Product ID
  hw_usb_device_class_t device_class;       ///< USB device class code
  hw_usb_device_subclass_t device_subclass; ///< USB device subclass code
  hw_usb_device_protocol_t device_protocol; ///< USB device protocol code
  uint8_t interface_number; ///< This interface's number, or `0xFF` if
                            ///< interface information wasn't available (see
                            ///< this file's own top-level doc) - in that
                            ///< case @p interface_class/_subclass/_protocol
                            ///< below are just copies of @p device_class/
                            ///< _subclass/_protocol above.
  hw_usb_device_class_t interface_class;       ///< USB interface class code
  hw_usb_device_subclass_t interface_subclass; ///< USB interface subclass
  hw_usb_device_protocol_t interface_protocol; ///< USB interface protocol
  char manufacturer[HW_USB_STRING_MAX_LENGTH + 1]; ///< Manufacturer string
  char product[HW_USB_STRING_MAX_LENGTH + 1];      ///< Product string
  char serial[HW_USB_STRING_MAX_LENGTH + 1];       ///< Serial number string
} hw_usb_device_t;

/**
 * @brief Opaque USB host handle.
 * @ingroup USB
 */
typedef struct hw_usb_t hw_usb_t;

/**
 * @brief Callback invoked on USB hotplug events.
 * @ingroup USB
 *
 * @param usb    The USB host handle.
 * @param event  The hotplug event type (attached or detached).
 * @param device Descriptor of the interface that was attached or detached.
 *               For normal attach/detach callbacks this is non-NULL. A
 *               device with multiple interfaces fires this callback once
 *               per interface (see this file's own top-level doc) - each
 *               of those calls shares the same @p device's device_id/vid/
 *               pid/strings, but carries that interface's own
 *               interface_number/interface_class/_subclass/_protocol.
 *               After initial enumeration completes, the callback is invoked
 *               once with @ref hw_usb_event_attached and @p device set to
 *               NULL as an "enumeration complete" marker.
 *               String fields may be empty on detach.
 * @param userdata Opaque user pointer supplied to @ref hw_usb_set_callback.
 */
typedef void (*hw_usb_callback_t)(hw_usb_t *usb, hw_usb_event_t event,
                                  const hw_usb_device_t *device,
                                  void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize the USB host subsystem.
 * @ingroup USB
 *
 * Initializes the USB host controller. The returned handle has no callback
 * attached - enumeration and hotplug detection still happen, just silently,
 * until one is attached via @ref hw_usb_set_callback.
 *
 * @return A USB host handle, or NULL if initialization fails.
 */
hw_usb_t *hw_usb_init(void);

/**
 * @brief Deinitialize the USB host subsystem.
 * @ingroup USB
 *
 * Shuts down the USB host controller and releases all associated resources.
 * The hotplug callback is deregistered and will not be invoked after this
 * call returns. Safe to call on an already-deinitialized handle, in which
 * case it is a no-op.
 *
 * @param usb The USB host handle to deinitialize.
 */
void hw_usb_deinit(hw_usb_t *usb);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Attach or detach the USB hotplug callback.
 * @ingroup USB
 *
 * @param usb Handle from @ref hw_usb_init.
 * @param callback Callback to invoke on attach/detach events, or NULL to
 * detach the current callback.
 * @param userdata Opaque user pointer forwarded to @p callback.
 *
 * Separate from init so that whichever part of a program brought USB up
 * doesn't have to be the same part that observes it - see this file's own
 * top-level doc, and @ref hw_usb_register_hid for exactly that use. Once
 * attached, the callback is fired with @ref hw_usb_event_attached for each
 * device already connected, then once more with @ref hw_usb_event_attached
 * and @p device set to NULL to mark enumeration complete, then again
 * whenever a device is physically attached or detached from then on. Safe
 * to call at any time. A no-op on an invalid handle.
 */
void hw_usb_set_callback(hw_usb_t *usb, hw_usb_callback_t callback,
                         void *userdata);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// HID INTEGRATION

/** @name HID Integration
 * @{ */

/**
 * @brief Register a USB host hotplug observer as a HID source.
 * @ingroup USB
 * @param instance HID instance that owns the registration.
 * @param usb USB handle from @ref hw_usb_init. Ownership isn't
 * transferred - the caller remains responsible for @ref hw_usb_deinit,
 * which this doesn't call.
 * @return Registered HID device descriptor, or NULL on failure (@p usb is
 * NULL, or a USB HID source is already registered - like @p usb itself,
 * this is a singleton, only one registration can be active at a time).
 *
 * Attaches a callback via @ref hw_usb_set_callback (replacing whatever was
 * attached before) and forwards every event it fires as a
 * `hid_event_type_usb` event - see `hid_usb_t`. Deregistering (via the
 * owning `hid_t`'s own teardown) detaches the callback but does not
 * deinitialize @p usb.
 */
hid_device_t *hw_usb_register_hid(hid_t *instance, hw_usb_t *usb);

/** @} */
