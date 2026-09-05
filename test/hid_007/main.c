#include <picofuse/hid.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hid_register_adc()/hid_register_temperature(): registration, metadata,
// and change-detected metric events via a real ADC channel and the
// internal temperature sensor - see test/hw_003 for the underlying
// hw_adc_* lifecycle this builds on. Pico-only, same gating as hw_003.
test_main_hw(0) {
  if (hw_adc_count() == 0) {
    sys_event_queue_t *queue = sys_event_queue_init(4);
    hid_t *instance = hid_init(queue);
    test_assert(hid_register_adc(instance, 0, NULL, 0, 0, NULL) == NULL);
    test_assert(hid_register_temperature(instance, 0, NULL) == NULL);
    hid_deinit(instance);
    sys_event_queue_deinit(queue);
    return;
  }

  sys_event_queue_t *queue = sys_event_queue_init(16);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  // A channel with no GPIO mapping is rejected outright.
  test_assert(hid_register_adc(instance, hw_adc_count(), NULL, 0, 0, NULL) ==
              NULL);

  // hid_register_adc() against channel 0 - fast polling interval so the
  // test doesn't take the full 5s default.
  int adc_userdata_sentinel = 0;
  hid_device_t *adc_device = hid_register_adc(
      instance, 0, "test_adc", 0, 10, &adc_userdata_sentinel);
  test_assert(adc_device != NULL);

  const char *name = NULL;
  hid_type_t type = hid_type_none;
  hid_class_t hid_class = hid_class_unknown;
  test_assert(hid_device_info(adc_device, &name, NULL, &type, &hid_class));
  test_assert_strequal(name, "test_adc");
  test_assert(type == hid_type_other);
  test_assert(hid_class == hid_class_sensor);
  test_assert(hid_device_userdata(adc_device) == &adc_userdata_sentinel);
  test_assert(hid_device_handle(adc_device) != NULL); // the hw_adc_t*

  // Poll until a metric event shows up - a lightly loaded/floating ADC
  // pin is noisy enough that its raw reading changes within a handful of
  // samples.
  hid_event_t *event = NULL;
  for (int i = 0; i < 50 && event == NULL; i++) {
    hid_poll(instance);
    event = (hid_event_t *)sys_event_queue_try_pop(queue);
    if (event == NULL) {
      sys_sleep_ms(10);
    }
  }
  test_assert(event != NULL);
  test_assert(event->type == hid_event_type_metric);
  test_assert(event->device == adc_device);
  test_assert_strequal(event->data.metric.name, "test_adc");
  hid_event_free(event);

  test_assert(hid_deregister(instance, adc_device));

  // hid_register_temperature()
  int temp_userdata_sentinel = 0;
  hid_device_t *temp_device =
      hid_register_temperature(instance, 10, &temp_userdata_sentinel);
  test_assert(temp_device != NULL);
  test_assert(hid_device_info(temp_device, &name, NULL, &type, &hid_class));
  test_assert_strequal(name, "temp");
  test_assert(type == hid_type_other);
  test_assert(hid_class == hid_class_sensor);
  test_assert(hid_device_userdata(temp_device) == &temp_userdata_sentinel);
  test_assert(hid_device_handle(temp_device) != NULL); // the hw_adc_t*

  // Best-effort: the internal temperature sensor changes far more slowly
  // than a floating ADC pin, so a change within this window isn't
  // guaranteed - just confirm nothing crashes and any event that does
  // arrive is well-formed and in a plausible range.
  for (int i = 0; i < 20; i++) {
    hid_poll(instance);
    hid_event_t *temp_event = (hid_event_t *)sys_event_queue_try_pop(queue);
    if (temp_event != NULL) {
      test_assert(temp_event->type == hid_event_type_metric);
      test_assert(temp_event->device == temp_device);
      test_assert_strequal(temp_event->data.metric.name, "temp");
      test_assert(temp_event->data.metric.value >= -40.0f &&
                  temp_event->data.metric.value <= 125.0f);
      hid_event_free(temp_event);
    }
    sys_sleep_ms(10);
  }

  test_assert(hid_deregister(instance, temp_device));

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}
