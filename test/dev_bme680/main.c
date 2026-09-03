#include <picofuse/dev.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

static dev_bme680_t *_try_i2c(uint8_t addr, hw_deviceio_t **device_out) {
  if (hw_i2c_count() == 0) {
    return NULL;
  }

  hw_deviceio_t *device = hw_i2c_init_default(addr);
  if (device == NULL) {
    return NULL;
  }
  if (!hw_i2c_detect(device, addr)) {
    hw_deviceio_deinit(device);
    return NULL;
  }

  dev_bme680_t *bme680 = dev_bme680_init(device, NULL);
  if (bme680 == NULL) {
    // Something acked the bus scan but didn't validate as a BME680.
    hw_deviceio_deinit(device);
    return NULL;
  }

  *device_out = device;
  return bme680;
}

static dev_bme680_t *_try_spi(hw_deviceio_t **device_out) {
  if (hw_spi_count() == 0) {
    return NULL;
  }

  hw_deviceio_t *device = hw_spi_init_default(1000000, NULL);
  if (device == NULL) {
    return NULL;
  }

  dev_bme680_t *bme680 = dev_bme680_init(device, NULL);
  if (bme680 == NULL) {
    hw_deviceio_deinit(device);
    return NULL;
  }

  *device_out = device;
  return bme680;
}

test_main_hw(0) {
  hw_deviceio_t *device = NULL;
  dev_bme680_t *bme680 = _try_i2c(DEV_BME680_I2C_ADDR_PRIMARY, &device);
  if (bme680 == NULL) {
    bme680 = _try_i2c(DEV_BME680_I2C_ADDR_SECONDARY, &device);
  }
  if (bme680 == NULL) {
    bme680 = _try_spi(&device);
  }

  if (bme680 == NULL) {
    sys_printf("[dev_bme680] no BME680 detected on I2C or SPI\n");
    return;
  }

  uint8_t chip_id = dev_bme680_chip_id(bme680);
  sys_printf("[dev_bme680] chip_id=0x%02X\n", chip_id);
  test_assert(chip_id == DEV_BME680_CHIP_ID);

  // A few readings in a row
  for (int i = 0; i < 5; i++) {
    dev_bme680_data_t data = {0};
    test_assert(dev_bme680_read(bme680, &data));
    sys_printf(
        "[dev_bme680] [%d] temperature=%.2fC pressure=%.1fPa humidity=%.2f%% "
        "gas=%.1fohm\n",
        i, (double)data.temperature_c, (double)data.pressure_pa,
        (double)data.humidity_pct, (double)data.gas_resistance_ohms);

    // BME680 datasheet operating ranges: -40..85C, 300..1100hPa
    // (30000..110000Pa), 0..100%RH. Gas resistance has no documented bound -
    // just check it's non-negative, mostly to guard against a NaN/inf
    // slipping through the compensation math undetected.
    test_assert(data.temperature_c >= -40.0f && data.temperature_c <= 85.0f);
    test_assert(data.pressure_pa >= 30000.0f && data.pressure_pa <= 110000.0f);
    test_assert(data.humidity_pct >= 0.0f && data.humidity_pct <= 100.0f);
    test_assert(data.gas_resistance_ohms >= 0.0f);

    sys_sleep_ms(500);
  }

  dev_bme680_deinit(bme680);
  hw_deviceio_deinit(device);
}
