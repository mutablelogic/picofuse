#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_i2c_detect() swept across every 7-bit address (0x00 - 0x7F): every
// address outside the valid 0x08-0x77 range must be rejected outright (no
// bus access at all, so this can't be masking a real ack), a NULL/non-I2C
// handle is rejected regardless of address, and every address inside the
// valid range is probed and reported - printed out, not just counted,
// since a populated bus makes this a genuine device inventory rather than
// just a "doesn't hang" check. If the platform has no I2C backend at all
// (host stubs), hw_i2c_count() reports 0 and there's nothing to exercise.
test_main_hw(0) {
  uint8_t count = hw_i2c_count();
  sys_debugf("hw_007", "hw_i2c_count() = %u", count);
  if (count == 0) {
    return;
  }

  hw_deviceio_t *dev = hw_i2c_init_default(0x50);
  test_assert(dev != NULL);

  test_assert(hw_i2c_detect(NULL, 0x50) == false);

  size_t detected = 0;
  for (uint16_t addr = 0; addr < 0x80; addr++) {
    bool ok = hw_i2c_detect(dev, (uint8_t)addr);
    if (addr < 0x08 || addr > 0x77) {
      test_assert(!ok);
      continue;
    }
    if (ok) {
      sys_debugf("hw_007", "detected device at address 0x%x", addr);
      detected++;
    }
  }
  sys_debugf("hw_007", "hw_i2c_detect() swept 0x00-0x7f, %zu acked", detected);

  hw_deviceio_deinit(dev);
}
