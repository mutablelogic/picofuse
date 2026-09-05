#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hid_register_timer(): repeating and one-shot timers, hid_device_userdata()
// unwrapping the caller's original userdata (not the underlying sys_timer_t
// handle), and the one-shot device pool slot staying alive until its event
// is freed (see timer.c's timer_remove_after_event dance).
test_main_sys(0) {
  sys_event_queue_t *queue = sys_event_queue_init(16);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  test_assert(hid_register_timer(instance, 1, 0, true, NULL) == NULL);

  // Repeating timer.
  int repeat_userdata = 0;
  hid_device_t *repeat_device =
      hid_register_timer(instance, 1, 20, true, &repeat_userdata);
  test_assert(repeat_device != NULL);
  test_assert(hid_device_userdata(repeat_device) == &repeat_userdata);

  for (int i = 0; i < 3; i++) {
    hid_event_t *event = (hid_event_t *)sys_event_queue_timed_pop(queue, 500);
    test_assert(event != NULL);
    test_assert(event->type == hid_event_type_timer);
    test_assert(event->device == repeat_device);
    test_assert(event->data.timer.userdata == &repeat_userdata);
    hid_event_free(event);
  }

  // A repeating timer never removes itself.
  test_assert(hid_device_info(repeat_device, NULL, NULL, NULL, NULL));
  test_assert(hid_deregister(instance, repeat_device));

  // sys_timer_deinit() (run by deregister's .deinit callback) blocks until
  // any in-flight fire is done, but one may have queued an event just
  // before that - drain it so it can't be mistaken for the one-shot
  // timer's event below.
  hid_event_t *stale;
  while ((stale = (hid_event_t *)sys_event_queue_try_pop(queue)) != NULL) {
    hid_event_free(stale);
  }

  // One-shot timer.
  int oneshot_userdata = 0;
  hid_device_t *oneshot_device =
      hid_register_timer(instance, 2, 20, false, &oneshot_userdata);
  test_assert(oneshot_device != NULL);

  hid_event_t *event = (hid_event_t *)sys_event_queue_timed_pop(queue, 500);
  test_assert(event != NULL);
  test_assert(event->type == hid_event_type_timer);
  test_assert(event->device == oneshot_device);
  test_assert(event->data.timer.userdata == &oneshot_userdata);

  // The pool slot is kept alive until the event is freed...
  test_assert(hid_device_info(oneshot_device, NULL, NULL, NULL, NULL));
  hid_event_free(event);
  // ...and released automatically once it is.
  test_assert(!hid_device_info(oneshot_device, NULL, NULL, NULL, NULL));

  // No second fire from the one-shot timer.
  test_assert(sys_event_queue_timed_pop(queue, 100) == NULL);

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}
