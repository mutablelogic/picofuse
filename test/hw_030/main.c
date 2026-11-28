#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// The internal temperature sensor on a host: on Linux, the kernel's thermal zone or
// hwmon sensor, if there is one, which reads something plausible, averaged or not. On
// a Mac, or in a container without one, there's none.
test_main_hw(0) {
  hw_adc_t *temp = hw_adc_init_temperature();
  if (temp == NULL) {
    sys_printf("[hw_030] no temperature sensor\n");
    test_assert(hw_adc_read_temperature(NULL, 1) == 0.0f);
    return;
  }
  float celsius = hw_adc_read_temperature(temp, 1);
  float averaged = hw_adc_read_temperature(temp, 8);
  sys_printf("[hw_030] %.1fC, averaged %.1fC\n", (double)celsius, (double)averaged);
  test_assert(celsius > 0.0f && celsius < 100.0f);
  test_assert(averaged > 0.0f && averaged < 100.0f);
  test_assert(hw_adc_read_temperature(temp, 0) > 0.0f); // 0 is one read
  test_assert(hw_adc_count() == 0);                     // no channels
  hw_adc_deinit(temp);
}
