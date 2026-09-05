#include <picofuse/hid.h>
#include <picofuse/sys.h>
#include <test/test.h>

typedef struct {
  int init_calls;
  int read_calls;
  int deinit_calls;
} _fake_state_t;

static bool _fake_init(hid_device_t *device, void *userdata) {
  (void)device;
  ((_fake_state_t *)userdata)->init_calls++;
  return true;
}

static bool _fake_read(hid_device_t *device, void *userdata) {
  (void)device;
  ((_fake_state_t *)userdata)->read_calls++;
  return true;
}

static bool _fake_deinit(hid_device_t *device, void *userdata) {
  (void)device;
  ((_fake_state_t *)userdata)->deinit_calls++;
  return true;
}

// hid_t is a process-wide singleton, and hid_register()'s generic path
// (a fake device with caller-supplied callbacks) is all that's wired up
// so far - no real backends (gpio/timer/signal/wifi/...) yet.
test_main_sys(0) {
  test_assert(hid_init(NULL) == NULL);

  sys_event_queue_t *queue = sys_event_queue_init(4);
  test_assert(queue != NULL);

  hid_t *instance = hid_init(queue);
  test_assert(instance != NULL);

  // A second hid_init() is rejected outright - there's only ever one
  // singleton instance.
  test_assert(hid_init(queue) == NULL);

  // No devices registered yet.
  test_assert(!hid_poll(instance));
  test_assert(hid_device_next(NULL) == NULL);

  _fake_state_t state = {0};
  hid_device_callbacks_t callbacks = {
      .init = _fake_init,
      .read = _fake_read,
      .deinit = _fake_deinit,
  };
  hid_device_t *device = hid_register(instance, "fake", 42, hid_type_other,
                                      hid_class_sensor, 0, &state, callbacks);
  test_assert(device != NULL);
  test_assert(state.init_calls == 1);

  const char *name = NULL;
  uint32_t id = 0;
  hid_type_t type = hid_type_none;
  hid_class_t hid_class = hid_class_unknown;
  test_assert(hid_device_info(device, &name, &id, &type, &hid_class));
  test_assert_strequal(name, "fake");
  test_assert(id == 42);
  test_assert(type == hid_type_other);
  test_assert(hid_class == hid_class_sensor);
  test_assert(hid_device_userdata(device) == &state);

  test_assert(hid_device_next(NULL) == device);
  test_assert(hid_device_next(device) == NULL);

  test_assert(hid_poll(instance));
  test_assert(state.read_calls == 1);

  test_assert(hid_deregister(instance, device));
  test_assert(state.deinit_calls == 1);
  test_assert(hid_device_next(NULL) == NULL);

  // A stale device pointer is rejected once deregistered.
  test_assert(!hid_deregister(instance, device));

  hid_deinit(instance);
  hid_deinit(NULL); // safe no-op

  sys_event_queue_deinit(queue);
}
