#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

// hid_event_owned(): a queue which holds HID's events and others, such as a run
// loop's, tells them apart
test_main_sys(0) {
  sys_event_queue_t *queue = sys_event_queue_init(16);
  test_assert(queue != NULL);
  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  // Something else in the same queue
  static int other = 0;
  test_assert(sys_event_queue_try_push(queue, &other));
  test_assert(!hid_event_owned(instance, &other));

  // A HID timer's event
  hid_device_t *device = hid_register_timer(instance, 1, 5, true, NULL);
  test_assert(device != NULL);
  sys_event_t event;
  do {
    event = sys_event_queue_timed_pop(queue, 500);
    test_assert(event != NULL);
  } while (event == &other);
  test_assert(hid_event_owned(instance, event));
  test_assert(((hid_event_t *)event)->type == hid_event_type_timer);

  // Not part way into an event, nor NULL, nor for no instance
  test_assert(!hid_event_owned(instance, (uint8_t *)event + 1));
  test_assert(!hid_event_owned(instance, NULL));
  test_assert(!hid_event_owned(NULL, event));

  // Once it's freed, its slot isn't in use
  hid_event_free(event);
  test_assert(!hid_event_owned(instance, event));

  test_assert(hid_deregister(instance, device));
  while ((event = sys_event_queue_try_pop(queue)) != NULL) {
    if (hid_event_owned(instance, event)) {
      hid_event_free(event);
    }
  }
  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}
