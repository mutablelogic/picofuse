#include <picofuse/hid.h>

// Weak fallbacks used when picofuse-hid is not linked into the final
// binary, so picofuse-app can call hid_init()/hid_deinit()/hid_poll()
// without forcing a hard dependency on the hid module. The real
// implementation (src/picofuse/hid/hid.c) defines strong symbols of the
// same names, which the linker prefers over these whenever it is present
// in the same link.

__attribute__((weak)) hid_t *hid_init(sys_event_queue_t *queue) {
  (void)queue;
  return NULL;
}

__attribute__((weak)) void hid_deinit(hid_t *instance) { (void)instance; }

__attribute__((weak)) bool hid_poll(hid_t *instance) {
  (void)instance;
  return false;
}
