#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

static uint64_t _abs_diff_u64(uint64_t a, uint64_t b) {
  return a > b ? a - b : b - a;
}

// hw_pwm_* configuration/control lifecycle and argument validation: NULL
// handles/pointers are rejected everywhere rather than crashing, a second
// hw_pwm_init() on the same GPIO while the first is still open is
// rejected, and period/duty/config get/set round-trip within the
// quantization tolerance the wrap/divider search in pico/pwm.c actually
// achieves (exact bit-for-bit round-tripping isn't the contract - see
// hw_pwm_set_period_ns()'s doc). If the platform has no PWM hardware at
// all (host stubs - hw_pwm_irq_supported() false there too, though that's
// not what's gating this test), hw_pwm_init() itself returns NULL and
// there's nothing to exercise.
//
// This test's own volume of back-to-back sys_debugf() calls used to make
// testrunner report a timeout here despite the target completing
// correctly every time (confirmed via GDB - it always reached
// sys_halt() normally). Root cause turned out to be in the printf layer
// itself, not this test or RTT specifically: _sys_fprintf_putch()
// (src/picofuse/sys/printf/vprintf.c) called sys_iostream_write() once
// per character and silently dropped it on a documented short/zero-byte
// write, which src/picofuse/sys/pico/stdio_uart.c's 256-byte ring buffer
// returns once full - now fixed to retry until each byte is actually
// accepted.
test_main_hw(0) {
  // NULL-safety: every setter/getter must tolerate an invalid handle.
  test_assert(hw_pwm_init(NULL, NULL, NULL, NULL) == NULL);
  test_assert(hw_pwm_set_period_ns(NULL, 1000000) == false);
  test_assert(hw_pwm_get_period_ns(NULL) == 0);
  test_assert(hw_pwm_set_duty_percent(NULL, 50.0f) == false);
  test_assert(hw_pwm_get_duty_percent(NULL) == 0.0f);
  test_assert(hw_pwm_set_config(NULL, NULL) == false);
  test_assert(hw_pwm_get_config(NULL, NULL) == false);
  test_assert(hw_pwm_get_enabled(NULL) == false);
  hw_pwm_set_enabled(NULL, true); // must not crash
  hw_pwm_deinit(NULL);            // must not crash

  hw_gpio_t *gpio = hw_gpio_init(0, 0, hw_gpio_none);
  test_assert(gpio != NULL);

  hw_pwm_t *pwm = hw_pwm_init(gpio, NULL, NULL, NULL);
  test_assert(pwm != NULL);

  // A second init on the same GPIO/slice/channel, while the first handle
  // is still open, is rejected rather than silently stealing it.
  test_assert(hw_pwm_init(gpio, NULL, NULL, NULL) == NULL);

  // Defaults (no config passed): disabled, 0% duty.
  test_assert(hw_pwm_get_enabled(pwm) == false);
  test_assert(hw_pwm_get_duty_percent(pwm) == 0.0f);
  test_assert(hw_pwm_get_period_ns(pwm) > 0);

  test_assert(hw_pwm_get_config(pwm, NULL) == false);

  // Period set/get - quantized to the nearest representable wrap/divider,
  // so allow 1% (or a small fixed floor for very short periods).
  uint64_t requested_ns = 1000000; // 1ms
  test_assert(hw_pwm_set_period_ns(pwm, requested_ns));
  uint64_t actual_ns = hw_pwm_get_period_ns(pwm);
  uint64_t tolerance_ns = requested_ns / 100 > 100 ? requested_ns / 100 : 100;
  sys_debugf("hw_010", "period requested_ns=%lu actual_ns=%lu", requested_ns,
             actual_ns);
  test_assert(_abs_diff_u64(actual_ns, requested_ns) <= tolerance_ns);

  // Duty set/get at representative points, including the two boundary
  // clamps documented for hw_pwm_set_duty_percent().
  float duty_points[] = {0.0f, 25.0f, 50.0f, 100.0f};
  for (size_t i = 0; i < sizeof(duty_points) / sizeof(duty_points[0]); i++) {
    test_assert(hw_pwm_set_duty_percent(pwm, duty_points[i]));
    float actual_duty = hw_pwm_get_duty_percent(pwm);
    sys_debugf("hw_010", "duty requested=%f actual=%f",
               (double)duty_points[i], (double)actual_duty);
    test_assert(actual_duty >= duty_points[i] - 1.0f &&
               actual_duty <= duty_points[i] + 1.0f);
  }

  // Out-of-range duty is clamped, not rejected. The 100% clamp reads back
  // as level/(wrap+1) - a hair under 100.0 for any finite wrap, since the
  // counter-compare level ranges over wrap+1 discrete values - so this
  // needs the same tolerance as the representative points above, not
  // exact equality.
  test_assert(hw_pwm_set_duty_percent(pwm, -10.0f));
  test_assert(hw_pwm_get_duty_percent(pwm) == 0.0f);
  test_assert(hw_pwm_set_duty_percent(pwm, 150.0f));
  float clamped_duty = hw_pwm_get_duty_percent(pwm);
  sys_debugf("hw_010", "duty requested=150.000000 (clamped) actual=%f",
             (double)clamped_duty);
  test_assert(clamped_duty > 99.0f && clamped_duty <= 100.0f);

  // Enable/disable round trip.
  hw_pwm_set_enabled(pwm, true);
  test_assert(hw_pwm_get_enabled(pwm) == true);
  hw_pwm_set_enabled(pwm, false);
  test_assert(hw_pwm_get_enabled(pwm) == false);

  // Whole-config set/get round trip.
  hw_pwm_config_t config = {
      .period_ns = 500000, // 0.5ms
      .duty_percent = 75.0f,
      .enabled = true,
  };
  test_assert(hw_pwm_set_config(pwm, &config));

  hw_pwm_config_t out_config = {0};
  test_assert(hw_pwm_get_config(pwm, &out_config));
  sys_debugf("hw_010",
             "config period_ns=%lu duty_percent=%f enabled=%u", out_config.period_ns,
             (double)out_config.duty_percent, out_config.enabled);
  test_assert(_abs_diff_u64(out_config.period_ns, config.period_ns) <=
             config.period_ns / 100);
  test_assert(out_config.duty_percent >= config.duty_percent - 1.0f &&
             out_config.duty_percent <= config.duty_percent + 1.0f);
  test_assert(out_config.enabled == true);

  hw_pwm_deinit(pwm);

  // The slot is free again once deinited - a fresh init on the same GPIO
  // must succeed.
  hw_pwm_t *pwm2 = hw_pwm_init(gpio, NULL, NULL, NULL);
  test_assert(pwm2 != NULL);

  hw_pwm_deinit(pwm2);
  hw_gpio_deinit(gpio);
}
