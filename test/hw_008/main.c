#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

// hw_spi_* lifecycle: count, default-adapter init, that
// hw_deviceio_xfr()/read_reg()/write_reg() report the length they were
// asked to move (SPI has no acknowledgment mechanism like I2C's NACK, so a
// transfer always "succeeds" at the byte-count level regardless of what -
// if anything - is actually connected; this isn't a check of the data
// itself), and that hw_spi_init() on an index already in use replaces the
// existing owner rather than requiring an explicit deinit first. If the
// platform has no SPI backend at all (host stubs), hw_spi_count() reports
// 0 and there's nothing to exercise.
test_main_hw(0) {
  uint8_t count = hw_spi_count();
  sys_debugf("hw_008", "hw_spi_count() = %u", count);
  if (count == 0) {
    return;
  }

  hw_deviceio_t *dev_a = hw_spi_init_default(1000000, NULL);
  test_assert(dev_a != NULL);

  uint8_t buf[4] = {0};
  test_assert(hw_deviceio_xfr(dev_a, buf, sizeof(buf), 0, 0) == sizeof(buf));
  test_assert(hw_deviceio_read_reg(dev_a, 0x00, buf, sizeof(buf), 0) ==
             sizeof(buf));
  test_assert(hw_deviceio_write_reg(dev_a, 0x00, buf, sizeof(buf), 0) ==
             sizeof(buf));

  // A second hw_spi_init_default() on the same default index replaces
  // dev_a rather than requiring it to be explicitly deinited first - dev_a
  // itself must not be touched again after this, since it's now deinited
  // out from under this call.
  hw_deviceio_t *dev_b = hw_spi_init_default(1000000, NULL);
  test_assert(dev_b != NULL);

  hw_deviceio_deinit(dev_b);
}
