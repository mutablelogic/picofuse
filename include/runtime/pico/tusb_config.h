/**
 * @file
 *
 * TinyUSB configuration - host mode only (see src/picofuse/hw/pico/usb.c).
 * picofuse doesn't use a TinyUSB device stack of its own, and doesn't claim
 * any interface-level class driver (HID/CDC/MSC all default to 0 below) -
 * hw_usb_t only needs device-level enumeration (vid/pid/class, string
 * descriptors, attach/detach), matching what the libusb backend exposes on
 * Linux/Darwin. Everything not set here (CFG_TUH_ENDPOINT_MAX,
 * CFG_TUH_INTERFACE_MAX, CFG_TUH_MEM_ALIGN, ...) keeps TinyUSB's own
 * built-in defaults - see tusb_option.h.
 */
#pragma once

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUH_ENABLED 1
#define CFG_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED

// One hub, up to 3 downstream devices each, plus one direct-attached device.
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX (3 * CFG_TUH_HUB + 1)
