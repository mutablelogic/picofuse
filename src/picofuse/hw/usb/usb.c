#include <picofuse/hw/usb.h>
#include <picofuse/sys.h>

// This whole symbolic lookup collapses to nothing in release builds -
// hw_usb_device_class_to_string()/hw_usb_device_protocol_to_string() fall
// straight through to the hex fallback below when NDEBUG is defined, so
// none of these tables get compiled in. Mirrors hid_keycode_to_string()'s
// own convention (see src/picofuse/hid/keycode.c).
#ifndef NDEBUG
#define HW_USB_ENUM_CASE(name)                                       \
  case name:                                                                 \
    return #name;

static const char *_hw_usb_device_class_name(hw_usb_device_class_t device_class) {
  switch (device_class) {
    HW_USB_ENUM_CASE(hw_usb_device_class_per_interface)
    HW_USB_ENUM_CASE(hw_usb_device_class_audio)
    HW_USB_ENUM_CASE(hw_usb_device_class_communications)
    HW_USB_ENUM_CASE(hw_usb_device_class_hid)
    HW_USB_ENUM_CASE(hw_usb_device_class_physical)
    HW_USB_ENUM_CASE(hw_usb_device_class_image)
    HW_USB_ENUM_CASE(hw_usb_device_class_printer)
    HW_USB_ENUM_CASE(hw_usb_device_class_mass_storage)
    HW_USB_ENUM_CASE(hw_usb_device_class_hub)
    HW_USB_ENUM_CASE(hw_usb_device_class_cdc_data)
    HW_USB_ENUM_CASE(hw_usb_device_class_smart_card)
    HW_USB_ENUM_CASE(hw_usb_device_class_content_security)
    HW_USB_ENUM_CASE(hw_usb_device_class_video)
    HW_USB_ENUM_CASE(hw_usb_device_class_personal_healthcare)
    HW_USB_ENUM_CASE(hw_usb_device_class_audio_video)
    HW_USB_ENUM_CASE(hw_usb_device_class_bluetooth)
    HW_USB_ENUM_CASE(hw_usb_device_class_miscellaneous)
    HW_USB_ENUM_CASE(hw_usb_device_class_application_specific)
    HW_USB_ENUM_CASE(hw_usb_device_class_vendor_specific)
  default:
    return NULL;
  }
}
#endif

const char *hw_usb_device_class_to_string(hw_usb_device_class_t device_class) {
#ifndef NDEBUG
  const char *name = _hw_usb_device_class_name(device_class);
  if (name != NULL) {
    return name;
  }
#endif
  static char buf[8];
  sys_sprintf(buf, sizeof(buf), "0x%02X", (unsigned int)device_class);
  return buf;
}

#ifndef NDEBUG
static const char *
_hw_usb_device_protocol_name(hw_usb_device_protocol_t device_protocol) {
  switch (device_protocol) {
    HW_USB_ENUM_CASE(hw_usb_device_protocol_none)
    HW_USB_ENUM_CASE(hw_usb_device_protocol_keyboard)
    HW_USB_ENUM_CASE(hw_usb_device_protocol_mouse)
  default:
    return NULL;
  }
}
#endif

const char *
hw_usb_device_protocol_to_string(hw_usb_device_protocol_t device_protocol) {
#ifndef NDEBUG
  const char *name = _hw_usb_device_protocol_name(device_protocol);
  if (name != NULL) {
    return name;
  }
#endif
  static char buf[8];
  sys_sprintf(buf, sizeof(buf), "0x%02X", (unsigned int)device_protocol);
  return buf;
}
