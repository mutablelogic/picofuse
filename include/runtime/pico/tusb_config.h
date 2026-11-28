/**
 * @file
 *
 * TinyUSB configuration, for the one USB controller as either:
 *
 * - a device, with one CDC interface, for standard input and output over USB
 *   (see src/picofuse/sys/pico/stdio_usb.c), when PICOFUSE_USB is off - the
 *   same as the Pico SDK's own pico_stdio_usb configuration, which this one
 *   is found before on the include path
 * - a host (see src/picofuse/hw/pico/usb.c), with PICOFUSE_USB. picofuse
 *   doesn't claim any interface-level class driver (HID/CDC/MSC all default
 *   to 0 below) - hw_usb_t only needs device-level enumeration (vid/pid/class,
 *   string descriptors, attach/detach), matching what the libusb backend
 *   exposes on Linux/Darwin. Everything not set here (CFG_TUH_ENDPOINT_MAX,
 *   CFG_TUH_INTERFACE_MAX, CFG_TUH_MEM_ALIGN, ...) keeps TinyUSB's own
 *   built-in defaults - see tusb_option.h.
 */
#pragma once

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#if defined(PICOFUSE_STDIO_USB)
#include "pico/stdio_usb.h"

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE)
#define CFG_TUD_CDC (1)
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif
#ifndef CFG_TUD_CDC_EP_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#endif
// A vendor interface, for resetting into BOOTSEL, with the SDK's own driver
#define CFG_TUD_VENDOR (0)

#else

#define CFG_TUH_ENABLED 1
#define CFG_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED

// One hub, up to 3 downstream devices each, plus one direct-attached device.
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX (3 * CFG_TUH_HUB + 1)

#endif
