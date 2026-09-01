#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_i2c_* lifecycle: count, default-adapter init, reserved-address
// rejection, a second device joining the same bus, and that talking to an
// address (a real device may or may not actually be present at) returns
// promptly through hw_deviceio_xfr()/read_reg()/write_reg() rather than
// hanging. If the platform has no I2C backend at all (host stubs),
// hw_i2c_count() reports 0 and there's nothing to exercise.
test_main_hw(0) {
  uint8_t count = hw_i2c_count();
  sys_debugf("hw_006", "hw_i2c_count() = %u", count);
  if (count == 0) {
    return;
  }

  // Reserved 7-bit address blocks (0000xxx, 1111xxx) are rejected outright.
  test_assert(hw_i2c_init_default(0x00) == NULL);
  test_assert(hw_i2c_init_default(0x78) == NULL);

  hw_deviceio_t *dev_a = hw_i2c_init_default(0x50);
  test_assert(dev_a != NULL);

  // A second device on the same bus.
  hw_deviceio_t *dev_b = hw_i2c_init_default(0x51);
  test_assert(dev_b != NULL);

  // Whether or not anything real acks at 0x50, the call must return
  // promptly with a sane result (0 on failure/NACK, or the full length),
  // not hang - the 50ms timeout is what guarantees that.
  uint8_t buf[4] = {0};
  size_t read = hw_deviceio_read_reg(dev_a, 0x00, buf, sizeof(buf), 50);
  test_assert(read == 0 || read == sizeof(buf));
  size_t written = hw_deviceio_write_reg(dev_a, 0x00, buf, sizeof(buf), 50);
  test_assert(written == 0 || written == sizeof(buf));

  hw_deviceio_deinit(dev_a);
  hw_deviceio_deinit(dev_b);

  // Both devices closed, so the bus is fully torn down - a fresh init
  // must still succeed.
  hw_deviceio_t *dev_c = hw_i2c_init_default(0x50);
  test_assert(dev_c != NULL);
  hw_deviceio_deinit(dev_c);
}
