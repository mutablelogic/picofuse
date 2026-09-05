#include "private.h"
#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stddef.h>

// Default polling interval when the caller passes 0, per hid_register_adc()/
// _temperature()'s own doc.
#define HID_ADC_DEFAULT_POLLING_INTERVAL_MS 5000u

// Fixed sample count for hid_register_temperature() - hid_register_adc()
// takes this from the caller instead, since it has no equivalent internal
// default to reuse.
#define HID_ADC_TEMPERATURE_SAMPLES 8u

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Overlaid onto hid_device_t.context (see HID_DEVICE_CONTEXT_SIZE) - no
// heap allocation needed for this per-device state.
typedef struct {
  hw_adc_t *adc;
  hw_gpio_t *gpio;   // NULL for the internal temperature channel
  uint16_t num_samples;
  uint16_t last_raw16;  // change-detection baseline for hid_register_adc()
  float last_temp_c;    // change-detection baseline for _temperature()
} _hid_adc_ctx_t;

_Static_assert(sizeof(_hid_adc_ctx_t) <= HID_DEVICE_CONTEXT_SIZE,
              "_hid_adc_ctx_t must fit in hid_device_t.context - see "
              "HID_DEVICE_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

static bool _hid_adc_device_deinit(hid_device_t *device, void *userdata) {
  (void)userdata;
  _hid_adc_ctx_t *ctx = (_hid_adc_ctx_t *)device->context;
  hw_adc_deinit(ctx->adc);
  if (ctx->gpio != NULL) {
    hw_gpio_deinit(ctx->gpio);
  }
  return true;
}

static bool _hid_adc_read(hid_device_t *device, void *userdata) {
  (void)userdata;
  _hid_adc_ctx_t *ctx = (_hid_adc_ctx_t *)device->context;

  uint16_t raw = hw_adc_read_16(ctx->adc, ctx->num_samples);
  if (raw == ctx->last_raw16) {
    return false;
  }
  ctx->last_raw16 = raw;
  return hid_event_queue_metric_float(device, device->name, "", (float)raw);
}

static bool _hid_temperature_read(hid_device_t *device, void *userdata) {
  (void)userdata;
  _hid_adc_ctx_t *ctx = (_hid_adc_ctx_t *)device->context;

  float value = hw_adc_read_temperature(ctx->adc, ctx->num_samples);
  if (value == ctx->last_temp_c) {
    return false;
  }
  ctx->last_temp_c = value;
  return hid_event_queue_metric_float(device, "temp", "C", value);
}

static const hid_device_callbacks_t _hid_adc_callbacks = {
    .read = _hid_adc_read,
    .deinit = _hid_adc_device_deinit,
};

static const hid_device_callbacks_t _hid_temperature_callbacks = {
    .read = _hid_temperature_read,
    .deinit = _hid_adc_device_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// METHODS

hid_device_t *hid_register_adc(hid_t *instance, uint8_t channel,
                               const char *metric_name, uint16_t num_samples,
                               uint32_t polling_interval_ms, void *userdata) {
  uint8_t pin = hw_adc_gpio_pin(channel);
  if (pin == 0xFF) {
    sys_debugf("hid", "adc register failed: channel=%u has no gpio pin",
               (unsigned)channel);
    return NULL;
  }

  hw_gpio_t *gpio = hw_gpio_init(0, pin, hw_gpio_adc);
  if (gpio == NULL) {
    return NULL;
  }

  hw_adc_t *adc = hw_adc_init_pin(gpio);
  if (adc == NULL) {
    hw_gpio_deinit(gpio);
    return NULL;
  }

  hid_device_t *device = hid_register(
      instance, metric_name != NULL ? metric_name : "raw_16", channel,
      hid_type_other, hid_class_sensor,
      polling_interval_ms != 0 ? polling_interval_ms
                               : HID_ADC_DEFAULT_POLLING_INTERVAL_MS,
      userdata, _hid_adc_callbacks);
  if (device == NULL) {
    hw_adc_deinit(adc);
    hw_gpio_deinit(gpio);
    return NULL;
  }

  _hid_adc_ctx_t *ctx = (_hid_adc_ctx_t *)device->context;
  ctx->adc = adc;
  ctx->gpio = gpio;
  ctx->num_samples = num_samples;
  return device;
}

hid_device_t *hid_register_temperature(hid_t *instance,
                                       uint32_t polling_interval_ms,
                                       void *userdata) {
  hw_adc_t *adc = hw_adc_init_temperature();
  if (adc == NULL) {
    return NULL;
  }

  hid_device_t *device = hid_register(
      instance, "temp", 0, hid_type_other, hid_class_sensor,
      polling_interval_ms != 0 ? polling_interval_ms
                               : HID_ADC_DEFAULT_POLLING_INTERVAL_MS,
      userdata, _hid_temperature_callbacks);
  if (device == NULL) {
    hw_adc_deinit(adc);
    return NULL;
  }

  _hid_adc_ctx_t *ctx = (_hid_adc_ctx_t *)device->context;
  ctx->adc = adc;
  ctx->gpio = NULL;
  ctx->num_samples = HID_ADC_TEMPERATURE_SAMPLES;
  return device;
}
