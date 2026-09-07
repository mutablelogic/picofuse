#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>
#include <tusb.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_usb_t {
  hw_usb_callback_t callback;
  void *userdata;
  bool init;
  bool enumeration_complete;
  uint64_t init_time_ms;
};

// Per-interface details for one already-attached device, compact enough to
// cache many of without the ~200-byte string-carrying hw_usb_device_t cost
// per interface - see hw_usb_device_cache_t's own doc.
typedef struct {
  uint8_t interface_number;
  hw_usb_device_class_t interface_class;
  hw_usb_device_subclass_t interface_subclass;
  hw_usb_device_protocol_t interface_protocol;
} hw_usb_interface_entry_t;

// What a device attach was actually built from - needed again at detach,
// since enumeration happens at the interface level (see hw/usb.h's own
// top-level doc): tuh_umount_cb() only gets a daddr, with the device
// already gone, so there's no way to re-derive its interface list at that
// point - it has to be remembered from tuh_mount_cb() instead.
typedef struct {
  bool used;
  uint8_t daddr;
  hw_usb_device_t base; // shared fields - interface_* left unset
  uint8_t count;
  hw_usb_interface_entry_t interfaces[HW_USB_INTERFACE_MAX_COUNT];
} hw_usb_device_cache_t;

/**
 * @def _HW_USB_ENUM_GRACE_MS
 * @brief Grace period given to TinyUSB to enumerate whatever's already
 * attached before firing the "enumeration complete" marker.
 *
 * Unlike the libusb backend's own one-shot device list (see
 * hw/libusb/usb.c's _hw_usb_emit_attached_devices()), which reads devices
 * the OS already recognizes synchronously with no wait at all, TinyUSB's
 * own per-device enumeration is asynchronous - it takes several tuh_task()
 * polls to drive the SET_ADDRESS/GET_DESCRIPTOR exchanges - so there's no
 * way to know "the initial burst is done" other than waiting a bit.
 */
#ifndef _HW_USB_ENUM_GRACE_MS
#define _HW_USB_ENUM_GRACE_MS 500u
#endif

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct hw_usb_t _hw_usb_instance = {0};
static struct hw_usb_t *_hw_usb_active = NULL;
static hw_usb_device_cache_t _hw_usb_cache[16] = {0};
static const uint16_t _hw_usb_language_id = 0x0409;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

static void _hw_usb_cache_clear(void) {
  memset(_hw_usb_cache, 0, sizeof(_hw_usb_cache));
}

static hw_usb_device_cache_t *_hw_usb_cache_get(uint8_t daddr) {
  for (size_t i = 0; i < (sizeof(_hw_usb_cache) / sizeof(_hw_usb_cache[0]));
       ++i) {
    if (_hw_usb_cache[i].used && _hw_usb_cache[i].daddr == daddr) {
      return &_hw_usb_cache[i];
    }
  }

  return NULL;
}

// `interfaces` is an array of `count` full hw_usb_device_t entries (as
// built by _hw_usb_build_interfaces()) - only the compact interface_*
// fields from each are retained.
static void _hw_usb_cache_set(uint8_t daddr, const hw_usb_device_t *base,
                              const hw_usb_device_t *interfaces,
                              uint8_t count) {
  hw_usb_device_cache_t *entry = _hw_usb_cache_get(daddr);
  if (entry == NULL) {
    for (size_t i = 0; i < (sizeof(_hw_usb_cache) / sizeof(_hw_usb_cache[0]));
         ++i) {
      if (!_hw_usb_cache[i].used) {
        entry = &_hw_usb_cache[i];
        entry->used = true;
        entry->daddr = daddr;
        break;
      }
    }
  }

  if (entry == NULL) {
    return;
  }

  entry->base = *base;
  entry->count =
      count > HW_USB_INTERFACE_MAX_COUNT ? HW_USB_INTERFACE_MAX_COUNT : count;
  for (uint8_t i = 0; i < entry->count; i++) {
    entry->interfaces[i].interface_number = interfaces[i].interface_number;
    entry->interfaces[i].interface_class = interfaces[i].interface_class;
    entry->interfaces[i].interface_subclass = interfaces[i].interface_subclass;
    entry->interfaces[i].interface_protocol = interfaces[i].interface_protocol;
  }
}

static void _hw_usb_cache_remove(uint8_t daddr) {
  hw_usb_device_cache_t *entry = _hw_usb_cache_get(daddr);
  if (entry != NULL) {
    memset(entry, 0, sizeof(*entry));
  }
}

static void _hw_usb_copy_utf16_string_to_ascii(char *dst, size_t dst_size,
                                               const uint16_t *utf16_desc) {
  dst[0] = '\0';
  if (utf16_desc == NULL) {
    return;
  }

  uint8_t desc_len = (uint8_t)(utf16_desc[0] & 0xffu);
  if (desc_len < 2) {
    return;
  }

  size_t utf16_len = (size_t)(desc_len - 2u) / sizeof(uint16_t);
  size_t out = 0;
  for (size_t i = 0; i < utf16_len && (out + 1) < dst_size; ++i) {
    uint16_t ch = utf16_desc[i + 1];
    dst[out++] = (ch <= 0x7fu) ? (char)ch : '?';
  }

  dst[out] = '\0';
}

static void _hw_usb_fill_strings(uint8_t daddr, hw_usb_device_t *device) {
  uint16_t buffer[HW_USB_STRING_MAX_LENGTH + 2] = {0};

  if (tuh_descriptor_get_manufacturer_string_sync(
          daddr, _hw_usb_language_id, buffer, sizeof(buffer)) ==
      XFER_RESULT_SUCCESS) {
    _hw_usb_copy_utf16_string_to_ascii(device->manufacturer,
                                       sizeof(device->manufacturer), buffer);
  }

  if (tuh_descriptor_get_product_string_sync(daddr, _hw_usb_language_id,
                                             buffer,
                                             sizeof(buffer)) ==
      XFER_RESULT_SUCCESS) {
    _hw_usb_copy_utf16_string_to_ascii(device->product, sizeof(device->product),
                                       buffer);
  }

  if (tuh_descriptor_get_serial_string_sync(daddr, _hw_usb_language_id,
                                            buffer, sizeof(buffer)) ==
      XFER_RESULT_SUCCESS) {
    _hw_usb_copy_utf16_string_to_ascii(device->serial, sizeof(device->serial),
                                       buffer);
  }
}

static bool _hw_usb_build_device(uint8_t daddr, hw_usb_device_t *device) {
  uint16_t vid = 0;
  uint16_t pid = 0;
  if (!tuh_vid_pid_get(daddr, &vid, &pid)) {
    return false;
  }

  tusb_desc_device_t desc = {0};
  if (tuh_descriptor_get_device_sync(daddr, &desc, sizeof(desc)) !=
      XFER_RESULT_SUCCESS) {
    return false;
  }

  memset(device, 0, sizeof(*device));
  device->device_id = daddr;
  device->vid = vid;
  device->pid = pid;
  device->device_class = desc.bDeviceClass;
  device->device_subclass = desc.bDeviceSubClass;
  device->device_protocol = desc.bDeviceProtocol;

  _hw_usb_fill_strings(daddr, device);
  return true;
}

// Walks daddr's configuration descriptor, writing one hw_usb_device_t per
// interface (alternate setting 0 only - see hw/usb.h's own top-level doc)
// into out[], each a copy of *base with its own interface_number/
// interface_class/_subclass/_protocol filled in. Always writes at least
// one entry - a single interface_number=0xFF fallback, copying
// device_class/_subclass/_protocol into the interface_* fields, if the
// configuration descriptor isn't readable or declares no interfaces.
// Returns the number of entries written.
static uint8_t _hw_usb_build_interfaces(uint8_t daddr,
                                        const hw_usb_device_t *base,
                                        hw_usb_device_t *out,
                                        uint8_t out_cap) {
  // Same budget _hw_usb_fill_strings() uses via HW_USB_STRING_MAX_LENGTH -
  // matches CFG_TUH_ENUMERATION_BUFSIZE (see tusb_config.h).
  uint8_t buffer[256];
  uint8_t count = 0;

  if (tuh_descriptor_get_configuration_sync(daddr, 0, buffer,
                                            sizeof(buffer)) ==
      XFER_RESULT_SUCCESS) {
    const tusb_desc_configuration_t *cfg =
        (const tusb_desc_configuration_t *)buffer;
    uint16_t total_len = cfg->wTotalLength;
    if (total_len > sizeof(buffer)) {
      total_len = sizeof(buffer);
    }

    const uint8_t *p = buffer;
    const uint8_t *end = buffer + total_len;
    while (p < end && count < out_cap) {
      if (tu_desc_len(p) == 0) {
        break; // malformed descriptor - stop rather than loop forever
      }
      if (tu_desc_type(p) == TUSB_DESC_INTERFACE) {
        const tusb_desc_interface_t *itf = (const tusb_desc_interface_t *)p;
        if (itf->bAlternateSetting == 0) {
          out[count] = *base;
          out[count].interface_number = itf->bInterfaceNumber;
          out[count].interface_class =
              (hw_usb_device_class_t)itf->bInterfaceClass;
          out[count].interface_subclass =
              (hw_usb_device_subclass_t)itf->bInterfaceSubClass;
          out[count].interface_protocol =
              (hw_usb_device_protocol_t)itf->bInterfaceProtocol;
          count++;
        }
      }
      p = tu_desc_next(p);
    }
  }

  if (count > 0) {
    return count;
  }

  out[0] = *base;
  out[0].interface_number = 0xFF;
  out[0].interface_class = base->device_class;
  out[0].interface_subclass = base->device_subclass;
  out[0].interface_protocol = base->device_protocol;
  return 1;
}

static void _hw_usb_emit_enumeration_complete_if_ready(void) {
  if (_hw_usb_active == NULL || !_hw_usb_active->init ||
      _hw_usb_active->enumeration_complete) {
    return;
  }

  if ((sys_timestamp_ms() - _hw_usb_active->init_time_ms) <
      _HW_USB_ENUM_GRACE_MS) {
    return;
  }

  _hw_usb_active->enumeration_complete = true;
  if (_hw_usb_active->callback != NULL) {
    _hw_usb_active->callback(_hw_usb_active, hw_usb_event_attached, NULL,
                             _hw_usb_active->userdata);
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_usb_t *hw_usb_init(void) {
  sys_debugf("usb", "usb_init");
  hw_usb_deinit(&_hw_usb_instance);

  if (!tuh_inited()) {
    const tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUH_OPT_HIGH_SPEED ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
    };
    bool ok = tuh_rhport_init(0, &rh_init);
    sys_debugf("usb", "tuh_rhport_init: ok=%u tuh_inited=%u",
               (unsigned int)ok, (unsigned int)tuh_inited());
    if (!ok) {
      return NULL;
    }
  }

  memset(&_hw_usb_instance, 0, sizeof(_hw_usb_instance));
  _hw_usb_instance.init = true;
  _hw_usb_instance.init_time_ms = sys_timestamp_ms();

  _hw_usb_cache_clear();
  _hw_usb_active = &_hw_usb_instance;

  return &_hw_usb_instance;
}

void hw_usb_deinit(hw_usb_t *usb) {
  if (usb == NULL) {
    return;
  }

  // hw_usb_init() unconditionally deinits the singleton instance first to
  // clear any stale prior state; skip the log/teardown in that (typically
  // no-op) case so it doesn't read as an init immediately undone.
  if (usb->init) {
    sys_debugf("usb", "usb_deinit: usb=%p", (void *)usb);
    tuh_deinit(0);
  }

  if (_hw_usb_active == usb) {
    _hw_usb_active = NULL;
  }

  _hw_usb_cache_clear();
  memset(usb, 0, sizeof(*usb));
}

void hw_usb_set_callback(hw_usb_t *usb, hw_usb_callback_t callback,
                         void *userdata) {
  if (usb == NULL || !usb->init) {
    return;
  }

  bool became_attached = usb->callback == NULL && callback != NULL;
  usb->callback = callback;
  usb->userdata = userdata;

  if (!became_attached) {
    return;
  }

  // "Once attached" (see this function's own doc) applies to every
  // attach, not just one lucky enough to happen before
  // _hw_usb_emit_enumeration_complete_if_ready()'s own grace period
  // elapses - hw_usb_init() never takes a callback, so a caller that
  // attaches one afterward (the common case) would otherwise silently
  // miss both the already-mounted device list and the completion marker
  // if either already happened by the time this runs. Re-check usb->init
  // after every call into the callback - it may reentrantly call
  // hw_usb_deinit() (a natural reaction to "nothing I care about is
  // attached"), the same hazard tuh_mount_cb()/
  // _hw_usb_emit_enumeration_complete_if_ready() avoid by re-reading
  // _hw_usb_active fresh rather than trusting state survives a callback.
  for (size_t i = 0; i < (sizeof(_hw_usb_cache) / sizeof(_hw_usb_cache[0]));
       ++i) {
    if (!usb->init) {
      return;
    }
    if (!_hw_usb_cache[i].used) {
      continue;
    }
    for (uint8_t j = 0; j < _hw_usb_cache[i].count; j++) {
      if (!usb->init) {
        return;
      }
      hw_usb_device_t device = _hw_usb_cache[i].base;
      device.interface_number = _hw_usb_cache[i].interfaces[j].interface_number;
      device.interface_class = _hw_usb_cache[i].interfaces[j].interface_class;
      device.interface_subclass =
          _hw_usb_cache[i].interfaces[j].interface_subclass;
      device.interface_protocol =
          _hw_usb_cache[i].interfaces[j].interface_protocol;
      callback(usb, hw_usb_event_attached, &device, userdata);
    }
  }

  if (usb->init && usb->enumeration_complete) {
    callback(usb, hw_usb_event_attached, NULL, userdata);
  }
}

///////////////////////////////////////////////////////////////////////////////
// PLATFORM INTEGRATION

// Called from hw_poll() (see init.c) - single-core, non-blocking (TinyUSB's
// own OS_PICO osal never actually blocks inside tuh_task(), regardless of
// the internal timeout it asks for - see osal_pico.h's own
// osal_queue_receive()).
void _hw_usb_poll(void) {
  if (_hw_usb_active == NULL || !_hw_usb_active->init) {
    return;
  }

  tuh_task();
  _hw_usb_emit_enumeration_complete_if_ready();
}

void tuh_mount_cb(uint8_t daddr) {
  if (_hw_usb_active == NULL || !_hw_usb_active->init) {
    return;
  }

  hw_usb_device_t base = {0};
  if (!_hw_usb_build_device(daddr, &base)) {
    return;
  }

  hw_usb_device_t interfaces[HW_USB_INTERFACE_MAX_COUNT];
  uint8_t count = _hw_usb_build_interfaces(daddr, &base, interfaces,
                                           HW_USB_INTERFACE_MAX_COUNT);
  _hw_usb_cache_set(daddr, &base, interfaces, count);

  // Re-check _hw_usb_active fresh before every call - the callback may
  // reentrantly call hw_usb_deinit() (see hw_usb_set_callback()'s own doc
  // on this same hazard).
  for (uint8_t i = 0; i < count; i++) {
    if (_hw_usb_active == NULL || !_hw_usb_active->init ||
        _hw_usb_active->callback == NULL) {
      return;
    }
    _hw_usb_active->callback(_hw_usb_active, hw_usb_event_attached,
                             &interfaces[i], _hw_usb_active->userdata);
  }
}

void tuh_umount_cb(uint8_t daddr) {
  if (_hw_usb_active == NULL || !_hw_usb_active->init) {
    return;
  }

  hw_usb_device_cache_t *entry = _hw_usb_cache_get(daddr);
  if (entry == NULL) {
    if (_hw_usb_active->callback != NULL) {
      hw_usb_device_t device = {0};
      device.interface_number = 0xFF;
      _hw_usb_active->callback(_hw_usb_active, hw_usb_event_detached, &device,
                               _hw_usb_active->userdata);
    }
    return;
  }

  // Copy out of the cache before firing anything - the callback may
  // reentrantly call hw_usb_deinit(), which clears the whole cache.
  hw_usb_device_t base = entry->base;
  uint8_t count = entry->count;
  hw_usb_interface_entry_t interfaces[HW_USB_INTERFACE_MAX_COUNT];
  memcpy(interfaces, entry->interfaces, sizeof(interfaces));
  _hw_usb_cache_remove(daddr);

  for (uint8_t i = 0; i < count; i++) {
    if (_hw_usb_active == NULL || !_hw_usb_active->init ||
        _hw_usb_active->callback == NULL) {
      return;
    }
    hw_usb_device_t device = base;
    device.interface_number = interfaces[i].interface_number;
    device.interface_class = interfaces[i].interface_class;
    device.interface_subclass = interfaces[i].interface_subclass;
    device.interface_protocol = interfaces[i].interface_protocol;
    _hw_usb_active->callback(_hw_usb_active, hw_usb_event_detached, &device,
                             _hw_usb_active->userdata);
  }
}
