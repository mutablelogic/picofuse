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

typedef struct {
  bool used;
  uint8_t daddr;
  hw_usb_device_t device;
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

static void _hw_usb_cache_set(uint8_t daddr, const hw_usb_device_t *device) {
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

  if (entry != NULL) {
    entry->device = *device;
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
  device->vid = vid;
  device->pid = pid;
  device->device_class = desc.bDeviceClass;
  device->device_subclass = desc.bDeviceSubClass;
  device->device_protocol = desc.bDeviceProtocol;

  _hw_usb_fill_strings(daddr, device);
  return true;
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
    if (_hw_usb_cache[i].used) {
      callback(usb, hw_usb_event_attached, &_hw_usb_cache[i].device,
               userdata);
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

  hw_usb_device_t device = {0};
  if (!_hw_usb_build_device(daddr, &device)) {
    return;
  }

  _hw_usb_cache_set(daddr, &device);
  if (_hw_usb_active->callback != NULL) {
    _hw_usb_active->callback(_hw_usb_active, hw_usb_event_attached, &device,
                             _hw_usb_active->userdata);
  }
}

void tuh_umount_cb(uint8_t daddr) {
  if (_hw_usb_active == NULL || !_hw_usb_active->init) {
    return;
  }

  hw_usb_device_t device = {0};
  hw_usb_device_cache_t *entry = _hw_usb_cache_get(daddr);
  if (entry != NULL) {
    device = entry->device;
    _hw_usb_cache_remove(daddr);
  }

  if (_hw_usb_active->callback != NULL) {
    _hw_usb_active->callback(_hw_usb_active, hw_usb_event_detached, &device,
                             _hw_usb_active->userdata);
  }
}
