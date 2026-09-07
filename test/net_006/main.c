#include <picofuse/hid.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <test/test.h>

// net_ntp_register_hid() against a real NTP server, via a real hid_t
// instance and a fast polling interval (the real default -
// net_ntp_register_hid()'s own doc - is once an hour). See net_005's own
// comment on why a real reply is skipped rather than asserted.

#define NET_006_TIMEOUT_MS 3000
#define NET_006_POLL_INTERVAL_MS 100
#define NET_006_WAIT_MS (5 * 1000)
#define NET_006_POLL_MS 20

test_main_sys(0) {
  sys_event_queue_t *queue = sys_event_queue_init(8);
  test_assert(queue != NULL);
  hid_t *hid = hid_init(queue);
  test_assert(hid != NULL);

  net_ntp_t *ntp = net_ntp_init(NULL, 0, NET_006_TIMEOUT_MS);
  test_assert(ntp != NULL);

  hid_device_t *device =
      net_ntp_register_hid(hid, ntp, NET_006_POLL_INTERVAL_MS, (void *)0x1234);
  test_assert(device != NULL);

  // The singleton is exclusive - a second registration while this one is
  // active must fail.
  test_assert(net_ntp_register_hid(hid, ntp, NET_006_POLL_INTERVAL_MS,
                                   NULL) == NULL);

  // The caller's own userdata is preserved - this file's own bookkeeping
  // doesn't hijack that slot (see hid.c's own doc on why).
  test_assert(hid_device_userdata(device) == (void *)0x1234);

  bool got_time_event = false;
  uint64_t start = sys_timestamp_ms();
  while (!got_time_event && sys_timestamp_ms() - start < NET_006_WAIT_MS) {
    hid_poll(hid);
    sys_event_t event = sys_event_queue_try_pop(queue);
    if (event != NULL) {
      hid_event_t *hid_event = (hid_event_t *)event;
      if (hid_event->type == hid_event_type_time) {
        sys_printf("[net_006] time event: seconds=%lld\n",
                   (long long)hid_event->data.time.date.seconds);
        test_assert(hid_event->data.time.date.seconds > 1704067200);
        test_assert(hid_event->data.time.date.tzoffset == 0);
        got_time_event = true;
      }
      hid_event_free(hid_event);
    }
    sys_sleep_ms(NET_006_POLL_MS);
  }

  if (!got_time_event) {
    sys_printf("[net_006] no time event - no network route, skipping\n");
  }

  hid_deregister(hid, device);
  net_ntp_deinit(ntp);

  // The slot is reusable after deregistering.
  net_ntp_t *ntp2 = net_ntp_init(NULL, 0, NET_006_TIMEOUT_MS);
  test_assert(ntp2 != NULL);
  hid_device_t *device2 =
      net_ntp_register_hid(hid, ntp2, NET_006_POLL_INTERVAL_MS, NULL);
  test_assert(device2 != NULL);
  hid_deregister(hid, device2);
  net_ntp_deinit(ntp2);

  hid_deinit(hid);
  sys_event_queue_deinit(queue);
}
