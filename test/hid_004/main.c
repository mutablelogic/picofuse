#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <signal.h>
#include <test/test.h>

// hid_register_signal(): observes a real SIGTERM raised against this
// process on platforms with signal support (Darwin/Linux, via
// sys/posix/signal.c); returns NULL outright where there's none (Pico's
// sys/stub/signal.c, where sys_env_signalhandler() always fails). Also
// covers only one registration being allowed at a time, and that
// deregistering frees the process-wide slot back up for a new one.
test_main_sys(0) {
  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  int userdata_sentinel = 0;
  hid_device_t *device = hid_register_signal(instance, &userdata_sentinel);
  if (device == NULL) {
    sys_printf("[hid_004] no signal support on this platform\n");
    hid_deinit(instance);
    sys_event_queue_deinit(queue);
    return;
  }
  test_assert(hid_device_userdata(device) == &userdata_sentinel);

  // Only one signal-observer registration at a time.
  test_assert(hid_register_signal(instance, NULL) == NULL);

  raise(SIGTERM);

  hid_event_t *event = (hid_event_t *)sys_event_queue_timed_pop(queue, 1000);
  test_assert(event != NULL);
  test_assert(event->type == hid_event_type_signal);
  test_assert(event->device == device);
  test_assert(event->data.signal.signal == sys_env_signal_term);
  hid_event_free(event);

  test_assert(hid_deregister(instance, device));

  // Deregistering frees the process-wide slot back up for a new one.
  hid_device_t *device2 = hid_register_signal(instance, NULL);
  test_assert(device2 != NULL);
  test_assert(hid_deregister(instance, device2));

  hid_deinit(instance);
  sys_event_queue_deinit(queue);
}
