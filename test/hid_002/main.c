#include <picofuse/hid.h>
#include <string.h>
#include <test/test.h>

// hid_keycode_to_string(): symbolic name in debug builds, "0x%04X" fallback
// for an unrecognized code (and unconditionally once NDEBUG is defined).
test_main_sys(0) {
#ifndef NDEBUG
  test_assert_strequal(hid_keycode_to_string(KEYCODE_ENTER), "KEYCODE_ENTER");
  test_assert_strequal(hid_keycode_to_string(KEYCODE_A), "KEYCODE_A");
#endif
  test_assert_strequal(hid_keycode_to_string(0xBEEF), "0xBEEF");

  char buf[128];

  // No flags set.
  test_assert(hid_state_to_string(hid_state_none, buf, sizeof(buf)) == 4);
  test_assert_strequal(buf, "none");

  // A single flag.
  test_assert(hid_state_to_string(hid_state_on, buf, sizeof(buf)) == 2);
  test_assert_strequal(buf, "on");

  // Multiple flags, "|"-joined in declaration order.
  test_assert(hid_state_to_string(hid_state_on | hid_state_left_shift, buf,
                                  sizeof(buf)) == strlen("on|left_shift"));
  test_assert_strequal(buf, "on|left_shift");

  // Convenience masks (e.g. hid_state_shift) aren't tested as their own
  // flag - only the atomic left/right bits they're made of are, so both
  // show up individually.
  test_assert(hid_state_to_string(hid_state_shift, buf, sizeof(buf)) ==
              strlen("left_shift|right_shift"));
  test_assert_strequal(buf, "left_shift|right_shift");

  // NULL/zero-size buffer is a safe no-op.
  test_assert(hid_state_to_string(hid_state_on, NULL, sizeof(buf)) == 0);
  test_assert(hid_state_to_string(hid_state_on, buf, 0) == 0);
}
