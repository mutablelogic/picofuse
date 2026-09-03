#include "private.h"
#include <picofuse/dev/bme680.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct {
  uint16_t par_h1;
  uint16_t par_h2;
  int8_t par_h3;
  int8_t par_h4;
  int8_t par_h5;
  uint8_t par_h6;
  int8_t par_h7;

  int8_t par_gh1;
  int16_t par_gh2;
  int8_t par_gh3;

  uint16_t par_t1;
  int16_t par_t2;
  int8_t par_t3;

  uint16_t par_p1;
  int16_t par_p2;
  int8_t par_p3;
  int16_t par_p4;
  int16_t par_p5;
  int8_t par_p6;
  int8_t par_p7;
  int16_t par_p8;
  int16_t par_p9;
  uint8_t par_p10;

  int32_t t_fine;

  uint8_t res_heat_range;
  int8_t res_heat_val;
  int8_t range_sw_err;
} _dev_bme680_calib_t;

// device is not owned - see dev_bme680_init_i2c()/_spi()'s header doc.
struct dev_bme680_t {
  hw_deviceio_t *device;
  bool is_spi;
  uint8_t chip_id;
  uint8_t variant_id;
  dev_bme680_config_t config;
  _dev_bme680_calib_t calib;
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - REGISTER I/O

// BME680's SPI register addressing uses the address byte's MSB as a
// read/write flag rather than part of the address - I2C has no such bit.
// hw_deviceio_read_reg()/write_reg() send whatever address they're given
// as-is on both buses, so that flag is this driver's job to set/clear.
static bool _dev_bme680_write_register(dev_bme680_t *bme680, uint8_t reg,
                                       uint8_t value) {
  uint8_t addr = bme680->is_spi ? (reg & 0x7Fu) : reg;
  return hw_deviceio_write_reg(bme680->device, addr, &value, 1u, 0u) == 1u;
}

static bool _dev_bme680_read_registers(dev_bme680_t *bme680, uint8_t reg,
                                       uint8_t *data, size_t len) {
  uint8_t addr = bme680->is_spi ? (reg | 0x80u) : reg;
  return hw_deviceio_read_reg(bme680->device, addr, data, len, 0u) == len;
}

static bool _dev_bme680_read_register(dev_bme680_t *bme680, uint8_t reg,
                                      uint8_t *value) {
  return _dev_bme680_read_registers(bme680, reg, value, 1u);
}

// The BME680 can NAK during its power-on/reset settling window, so register
// I/O in dev_bme680_init() retries a few times rather than failing outright.
static bool _dev_bme680_write_register_retry(dev_bme680_t *bme680, uint8_t reg,
                                             uint8_t value) {
  for (uint8_t attempt = 0; attempt < BME680_IO_RETRY_COUNT; ++attempt) {
    if (_dev_bme680_write_register(bme680, reg, value)) {
      return true;
    }
    sys_sleep_ms(BME680_IO_RETRY_DELAY_MS);
  }
  return false;
}

static bool _dev_bme680_read_register_retry(dev_bme680_t *bme680, uint8_t reg,
                                            uint8_t *value) {
  for (uint8_t attempt = 0; attempt < BME680_IO_RETRY_COUNT; ++attempt) {
    if (_dev_bme680_read_register(bme680, reg, value)) {
      return true;
    }
    sys_sleep_ms(BME680_IO_RETRY_DELAY_MS);
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - CONFIGURATION

static uint8_t _dev_bme680_default_os(uint8_t os) {
  return os > BME680_OS_MAX ? 1u : os;
}

static uint8_t _dev_bme680_clamp_filter(uint8_t filter) {
  return filter > BME680_FILTER_MAX ? 0u : filter;
}

static uint8_t _dev_bme680_calc_gas_wait(uint16_t dur_ms) {
  uint8_t factor = 0;
  if (dur_ms >= 0xFC0u) {
    return 0xFFu;
  }
  while (dur_ms > 0x3Fu) {
    dur_ms /= 4u;
    factor += 1u;
  }
  return (uint8_t)(dur_ms + ((uint16_t)factor * 64u));
}

static uint8_t _dev_bme680_calc_res_heat(uint16_t temp_c,
                                         const dev_bme680_t *bme680) {
  if (temp_c > 400u) {
    temp_c = 400u;
  }

  int32_t ambient_temp_c = (int32_t)bme680->config.ambient_temp_c;
  int32_t var1 =
      ((ambient_temp_c * (int32_t)bme680->calib.par_gh3) / 1000) * 256;
  int32_t var2 =
      ((int32_t)bme680->calib.par_gh1 + 784) *
      ((((((int32_t)bme680->calib.par_gh2 + 154009) * (int32_t)temp_c * 5) /
         100) +
        3276800) /
       10);
  int32_t var3 = var1 + (var2 / 2);
  int32_t var4 = var3 / ((int32_t)bme680->calib.res_heat_range + 4);
  int32_t var5 = (131 * (int32_t)bme680->calib.res_heat_val) + 65536;
  int32_t heatr_res_x100 = (((var4 / var5) - 250) * 34);
  return (uint8_t)((heatr_res_x100 + 50) / 100);
}

// Soft reset + read calibration coefficients + write the ctrl_hum, config,
// and (if enabled) gas heater registers from bme680->config. Does not touch
// ctrl_meas - forced mode is only ever triggered by dev_bme680_read().
static bool _dev_bme680_configure(dev_bme680_t *bme680) {
  if (!_dev_bme680_write_register_retry(bme680, DEV_BME680_REG_SOFT_RESET,
                                        DEV_BME680_SOFT_RESET_CMD)) {
    sys_debugf("dev", "bme680: soft reset write failed");
    return false;
  }
  sys_sleep_ms(10);

  uint8_t coeff[BME680_LEN_COEFF_ALL] = {0};
  if (!_dev_bme680_read_registers(bme680, DEV_BME680_REG_COEFF1, coeff,
                                  BME680_LEN_COEFF1) ||
      !_dev_bme680_read_registers(bme680, DEV_BME680_REG_COEFF2,
                                  &coeff[BME680_LEN_COEFF1],
                                  BME680_LEN_COEFF2) ||
      !_dev_bme680_read_registers(bme680, DEV_BME680_REG_COEFF3,
                                  &coeff[BME680_LEN_COEFF1 + BME680_LEN_COEFF2],
                                  BME680_LEN_COEFF3)) {
    sys_debugf("dev", "bme680: calibration read failed");
    return false;
  }

  bme680->calib.par_t1 = BME680_CONCAT_BYTES(coeff[32], coeff[31]);
  bme680->calib.par_t2 = (int16_t)BME680_CONCAT_BYTES(coeff[1], coeff[0]);
  bme680->calib.par_t3 = (int8_t)coeff[2];

  bme680->calib.par_p1 = BME680_CONCAT_BYTES(coeff[5], coeff[4]);
  bme680->calib.par_p2 = (int16_t)BME680_CONCAT_BYTES(coeff[7], coeff[6]);
  bme680->calib.par_p3 = (int8_t)coeff[8];
  bme680->calib.par_p4 = (int16_t)BME680_CONCAT_BYTES(coeff[11], coeff[10]);
  bme680->calib.par_p5 = (int16_t)BME680_CONCAT_BYTES(coeff[13], coeff[12]);
  bme680->calib.par_p6 = (int8_t)coeff[15];
  bme680->calib.par_p7 = (int8_t)coeff[14];
  bme680->calib.par_p8 = (int16_t)BME680_CONCAT_BYTES(coeff[19], coeff[18]);
  bme680->calib.par_p9 = (int16_t)BME680_CONCAT_BYTES(coeff[21], coeff[20]);
  bme680->calib.par_p10 = coeff[22];

  bme680->calib.par_h1 = (uint16_t)(((uint16_t)coeff[25] << 4) |
                                    (coeff[24] & BME680_BIT_H1_DATA_MSK));
  bme680->calib.par_h2 =
      (uint16_t)(((uint16_t)coeff[23] << 4) | ((uint8_t)(coeff[24] >> 4)));
  bme680->calib.par_h3 = (int8_t)coeff[26];
  bme680->calib.par_h4 = (int8_t)coeff[27];
  bme680->calib.par_h5 = (int8_t)coeff[28];
  bme680->calib.par_h6 = coeff[29];
  bme680->calib.par_h7 = (int8_t)coeff[30];

  bme680->calib.par_gh1 = (int8_t)coeff[35];
  bme680->calib.par_gh2 = (int16_t)BME680_CONCAT_BYTES(coeff[34], coeff[33]);
  bme680->calib.par_gh3 = (int8_t)coeff[36];

  bme680->calib.res_heat_range =
      (uint8_t)((coeff[39] & BME680_RHRANGE_MSK) / 16u);
  bme680->calib.res_heat_val = (int8_t)coeff[37];
  bme680->calib.range_sw_err =
      (int8_t)((int8_t)(coeff[41] & BME680_RSERROR_MSK) / 16);

  uint8_t ctrl_hum =
      _dev_bme680_default_os(bme680->config.os_hum) & BME680_OSH_MSK;
  if (!_dev_bme680_write_register_retry(bme680, DEV_BME680_REG_CTRL_HUM,
                                        ctrl_hum)) {
    sys_debugf("dev", "bme680: ctrl_hum write failed");
    return false;
  }

  uint8_t config =
      BME680_SET_BITS(0u, BME680_FILTER_MSK, BME680_FILTER_POS,
                      _dev_bme680_clamp_filter(bme680->config.iir_filter));
  if (!_dev_bme680_write_register_retry(bme680, DEV_BME680_REG_CONFIG,
                                        config)) {
    sys_debugf("dev", "bme680: config write failed");
    return false;
  }

  if (bme680->config.gas_mode == dev_bme680_gas_disabled) {
    return true;
  }

  // A failure past this point leaves the sensor running with the gas
  // heater disabled rather than failing dev_bme680_init() outright -
  // temperature/pressure/humidity are still fully usable without it.
  uint8_t res_heat =
      _dev_bme680_calc_res_heat((uint16_t)bme680->config.heater_temp_c, bme680);
  uint8_t gas_wait =
      _dev_bme680_calc_gas_wait((uint16_t)bme680->config.heater_duration_ms);
  if (!_dev_bme680_write_register_retry(bme680, DEV_BME680_REG_RES_HEAT0,
                                        res_heat) ||
      !_dev_bme680_write_register_retry(bme680, DEV_BME680_REG_GAS_WAIT0,
                                        gas_wait)) {
    sys_debugf("dev", "bme680: heater profile write failed, gas disabled");
    return true;
  }

  uint8_t ctrl_gas_0 = BME680_SET_BITS(0u, BME680_HCTRL_MSK, BME680_HCTRL_POS,
                                       BME680_ENABLE_HEATER);
  if (!_dev_bme680_write_register_retry(bme680, DEV_BME680_REG_CTRL_GAS_0,
                                        ctrl_gas_0)) {
    sys_debugf("dev", "bme680: ctrl_gas_0 write failed, gas disabled");
    return true;
  }

  uint8_t run_gas = bme680->variant_id == DEV_BME680_VARIANT_GAS_HIGH
                        ? BME680_ENABLE_GAS_MEAS_HIGH
                        : BME680_ENABLE_GAS_MEAS_LOW;
  uint8_t ctrl_gas_1 = BME680_SET_BITS_POS_0(0u, BME680_NBCONV_MSK, 0u);
  ctrl_gas_1 = BME680_SET_BITS(ctrl_gas_1, BME680_RUN_GAS_MSK,
                               BME680_RUN_GAS_POS, run_gas);
  if (!_dev_bme680_write_register_retry(bme680, DEV_BME680_REG_CTRL_GAS_1,
                                        ctrl_gas_1)) {
    sys_debugf("dev", "bme680: ctrl_gas_1 write failed, gas disabled");
  }

  return true;
}

// Discards two measurements so the sensor (and its gas heater, if enabled)
// has settled before the caller ever sees a reading.
static bool _dev_bme680_warmup(dev_bme680_t *bme680) {
  dev_bme680_data_t data;
  if (!dev_bme680_read(bme680, &data)) {
    sys_debugf("dev", "bme680: warmup read 1 failed");
    return false;
  }
  if (!dev_bme680_read(bme680, &data)) {
    sys_debugf("dev", "bme680: warmup read 2 failed");
    return false;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - COMPENSATION
//
// Bosch's published trimming-coefficient formulas (BME680 datasheet
// section 3.3 / 3.5 / 3.6.1) - fixed-point in the original, kept that way
// here rather than reworked to float, so behavior matches the reference
// implementation bit-for-bit.

static int16_t _dev_bme680_compensate_temperature(uint32_t adc_temp,
                                                  dev_bme680_t *bme680) {
  int64_t var1 =
      ((int32_t)adc_temp >> 3) - ((int32_t)bme680->calib.par_t1 << 1);
  int64_t var2 = (var1 * (int32_t)bme680->calib.par_t2) >> 11;
  int64_t var3 = ((var1 >> 1) * (var1 >> 1)) >> 12;
  var3 = (var3 * ((int32_t)bme680->calib.par_t3 << 4)) >> 14;

  bme680->calib.t_fine = (int32_t)(var2 + var3);
  return (int16_t)(((bme680->calib.t_fine * 5) + 128) >> 8);
}

static uint32_t _dev_bme680_compensate_pressure(uint32_t adc_pres,
                                                const dev_bme680_t *bme680) {
  const int32_t pres_ovf_check = INT32_C(0x40000000);

  int32_t var1 = ((int32_t)bme680->calib.t_fine >> 1) - 64000;
  int32_t var2 =
      ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)bme680->calib.par_p6) >>
      2;
  var2 = var2 + ((var1 * (int32_t)bme680->calib.par_p5) << 1);
  var2 = (var2 >> 2) + ((int32_t)bme680->calib.par_p4 << 16);

  var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) *
           ((int32_t)bme680->calib.par_p3 << 5)) >>
          3) +
         (((int32_t)bme680->calib.par_p2 * var1) >> 1);
  var1 = var1 >> 18;
  var1 = ((32768 + var1) * (int32_t)bme680->calib.par_p1) >> 15;
  if (var1 == 0) {
    return 0;
  }

  int32_t pressure_comp = 1048576 - (int32_t)adc_pres;
  pressure_comp = (int32_t)((pressure_comp - (var2 >> 12)) * (uint32_t)3125);
  if (pressure_comp >= pres_ovf_check) {
    pressure_comp = (pressure_comp / var1) << 1;
  } else {
    pressure_comp = (pressure_comp << 1) / var1;
  }

  var1 = ((int32_t)bme680->calib.par_p9 *
          (int32_t)(((pressure_comp >> 3) * (pressure_comp >> 3)) >> 13)) >>
         12;
  var2 = ((int32_t)(pressure_comp >> 2) * (int32_t)bme680->calib.par_p8) >> 13;
  int32_t var3 =
      ((int32_t)(pressure_comp >> 8) * (int32_t)(pressure_comp >> 8) *
       (int32_t)(pressure_comp >> 8) * (int32_t)bme680->calib.par_p10) >>
      17;
  pressure_comp =
      pressure_comp +
      ((var1 + var2 + var3 + ((int32_t)bme680->calib.par_p7 << 7)) >> 4);

  return (uint32_t)pressure_comp;
}

static uint32_t _dev_bme680_compensate_humidity(uint16_t adc_hum,
                                                const dev_bme680_t *bme680) {
  int32_t temp_scaled = (((int32_t)bme680->calib.t_fine * 5) + 128) >> 8;
  int32_t var1 = (int32_t)adc_hum - ((int32_t)bme680->calib.par_h1 * 16) -
                 (((temp_scaled * (int32_t)bme680->calib.par_h3) / 100) >> 1);
  int32_t var2 = ((int32_t)bme680->calib.par_h2 *
                  (((temp_scaled * (int32_t)bme680->calib.par_h4) / 100) +
                   (((temp_scaled *
                      ((temp_scaled * (int32_t)bme680->calib.par_h5) / 100)) >>
                     6) /
                    100) +
                   (int32_t)(1 << 14))) >>
                 10;
  int32_t var3 = var1 * var2;
  int32_t var4 = (int32_t)bme680->calib.par_h6 << 7;
  var4 = (var4 + ((temp_scaled * (int32_t)bme680->calib.par_h7) / 100)) >> 4;
  int32_t var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
  int32_t var6 = (var4 * var5) >> 1;

  int32_t hum = (((var3 + var6) >> 10) * 1000) >> 12;
  if (hum > 100000) {
    hum = 100000;
  } else if (hum < 0) {
    hum = 0;
  }

  return (uint32_t)hum;
}

static uint32_t
_dev_bme680_calc_gas_resistance_low(uint16_t gas_res_adc, uint8_t gas_range,
                                    const dev_bme680_t *bme680) {
  static const uint32_t lookup_table1[16] = {
      2147483647u, 2147483647u, 2147483647u, 2147483647u,
      2147483647u, 2126008810u, 2147483647u, 2130303777u,
      2147483647u, 2147483647u, 2143188679u, 2136746228u,
      2147483647u, 2126008810u, 2147483647u, 2147483647u,
  };
  static const uint32_t lookup_table2[16] = {
      4096000000u, 2048000000u, 1024000000u, 512000000u, 255744255u, 127110228u,
      64000000u,   32258064u,   16016016u,   8000000u,   4000000u,   2000000u,
      1000000u,    500000u,     250000u,     125000u,
  };

  if (gas_range > 15u) {
    return 0u;
  }

  int64_t var1 = ((1340 + (5 * (int64_t)bme680->calib.range_sw_err)) *
                  (int64_t)lookup_table1[gas_range]) >>
                 16;
  int64_t var2 =
      ((int64_t)((int64_t)gas_res_adc << 15) - (int64_t)16777216) + var1;
  int64_t var3 = ((int64_t)lookup_table2[gas_range] * var1) >> 9;
  return (uint32_t)((var3 + (var2 >> 1)) / var2);
}

static uint32_t _dev_bme680_calc_gas_resistance_high(uint16_t gas_res_adc,
                                                     uint8_t gas_range) {
  if (gas_range > 31u) {
    return 0u;
  }

  uint32_t var1 = UINT32_C(262144) >> gas_range;
  int32_t var2 = ((int32_t)gas_res_adc - INT32_C(512)) * 3 + 4096;
  if (var2 <= 0) {
    return 0u;
  }

  return ((10000u * var1) / (uint32_t)var2) * 100u;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

void dev_bme680_default_config(dev_bme680_config_t *config) {
  if (config == NULL) {
    return;
  }

  *config = (dev_bme680_config_t){
      .os_temp = dev_bme680_oversampling_1x,
      .os_press = dev_bme680_oversampling_1x,
      .os_hum = dev_bme680_oversampling_1x,
      .iir_filter = dev_bme680_iir_filter_off,
      .heater_temp_c = dev_bme680_heater_temp_300c,
      .heater_duration_ms = dev_bme680_heater_dur_100ms,
      .ambient_temp_c = dev_bme680_ambient_temp_25c,
      .gas_mode = dev_bme680_gas_enabled,
  };
}

dev_bme680_t *dev_bme680_init(hw_deviceio_t *device,
                              const dev_bme680_config_t *config) {
  sys_debugf("dev", "bme680_init: device=%p config=%p", device, config);
  if (device == NULL) {
    return NULL;
  }

  dev_bme680_t *bme680 = sys_calloc(1, sizeof(*bme680));
  if (bme680 == NULL) {
    return NULL;
  }

  bme680->device = device;
  bme680->is_spi = hw_deviceio_bus(device) == hw_deviceio_spi;
  if (config != NULL) {
    bme680->config = *config;
  } else {
    dev_bme680_default_config(&bme680->config);
  }

  uint8_t chip_id = 0;
  if (!_dev_bme680_read_register(bme680, DEV_BME680_REG_CHIP_ID, &chip_id) ||
      chip_id != DEV_BME680_CHIP_ID) {
    sys_debugf("dev", "bme680_init: chip id check failed (got 0x%02X)",
               chip_id);
    dev_bme680_deinit(bme680);
    return NULL;
  }
  bme680->chip_id = chip_id;

  sys_sleep_ms(2);
  if (!_dev_bme680_read_register_retry(bme680, DEV_BME680_REG_VARIANT_ID,
                                       &bme680->variant_id)) {
    bme680->variant_id = DEV_BME680_VARIANT_GAS_LOW;
    sys_debugf("dev", "bme680_init: variant id read failed, defaulting to "
                      "gas-low");
  }

  if (!_dev_bme680_configure(bme680)) {
    sys_debugf("dev", "bme680_init: configure failed");
    dev_bme680_deinit(bme680);
    return NULL;
  }

  if (!_dev_bme680_warmup(bme680)) {
    sys_debugf("dev", "bme680_init: warmup failed");
    dev_bme680_deinit(bme680);
    return NULL;
  }

  return bme680;
}

void dev_bme680_deinit(dev_bme680_t *bme680) {
  sys_debugf("dev", "bme680_deinit: bme680=%p", bme680);
  if (bme680 == NULL) {
    return;
  }
  sys_free(bme680);
}

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

uint8_t dev_bme680_chip_id(const dev_bme680_t *bme680) {
  return bme680 != NULL ? bme680->chip_id : 0;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool dev_bme680_read(dev_bme680_t *bme680, dev_bme680_data_t *data) {
  if (bme680 == NULL || data == NULL) {
    return false;
  }

  uint8_t ctrl_meas = 0u;
  ctrl_meas = BME680_SET_BITS(ctrl_meas, BME680_OST_MSK, BME680_OST_POS,
                              _dev_bme680_default_os(bme680->config.os_temp));
  ctrl_meas = BME680_SET_BITS(ctrl_meas, BME680_OSP_MSK, BME680_OSP_POS,
                              _dev_bme680_default_os(bme680->config.os_press));
  ctrl_meas =
      BME680_SET_BITS_POS_0(ctrl_meas, BME680_MODE_MSK, BME680_FORCED_MODE);

  if (!_dev_bme680_write_register(bme680, DEV_BME680_REG_CTRL_MEAS,
                                  ctrl_meas)) {
    return false;
  }

  uint8_t field[BME680_LEN_FIELD] = {0};
  bool got_new_data = false;
  for (uint8_t attempt = 0; attempt < BME680_MEAS_RETRY_COUNT; ++attempt) {
    sys_sleep_ms(BME680_MEAS_POLL_MS);

    if (!_dev_bme680_read_registers(bme680, DEV_BME680_REG_FIELD0, field,
                                    sizeof(field))) {
      return false;
    }
    if ((field[0] & BME680_FIELD_NEW_DATA_MSK) != 0u) {
      got_new_data = true;
      break;
    }
  }
  if (!got_new_data) {
    return false;
  }

  uint32_t adc_pres = ((uint32_t)field[2] * 4096u) |
                      ((uint32_t)field[3] * 16u) | ((uint32_t)field[4] / 16u);
  uint32_t adc_temp = ((uint32_t)field[5] * 4096u) |
                      ((uint32_t)field[6] * 16u) | ((uint32_t)field[7] / 16u);
  uint16_t adc_hum = ((uint16_t)field[8] * 256u) | (uint16_t)field[9];
  uint16_t adc_gas_res_low =
      (uint16_t)(((uint32_t)field[13] * 4u) | ((uint32_t)field[14] / 64u));
  uint16_t adc_gas_res_high =
      (uint16_t)(((uint32_t)field[15] * 4u) | ((uint32_t)field[16] / 64u));
  uint8_t gas_range_l = field[14] & BME680_FIELD_GAS_RANGE_MSK;
  uint8_t gas_range_h = field[16] & BME680_FIELD_GAS_RANGE_MSK;

  uint8_t status = field[0];
  status =
      (uint8_t)(status | (bme680->variant_id == DEV_BME680_VARIANT_GAS_HIGH
                              ? (field[16] & (BME680_FIELD_GASM_VALID_MSK |
                                              BME680_FIELD_HEAT_STAB_MSK))
                              : (field[14] & (BME680_FIELD_GASM_VALID_MSK |
                                              BME680_FIELD_HEAT_STAB_MSK))));

  int16_t temp_x100 = _dev_bme680_compensate_temperature(adc_temp, bme680);
  uint32_t pressure_pa = _dev_bme680_compensate_pressure(adc_pres, bme680);
  uint32_t humidity_x1000 = _dev_bme680_compensate_humidity(adc_hum, bme680);

  uint32_t gas_ohms = 0u;
  if (bme680->config.gas_mode == dev_bme680_gas_enabled &&
      (status & BME680_FIELD_GASM_VALID_MSK) != 0u) {
    gas_ohms = bme680->variant_id == DEV_BME680_VARIANT_GAS_HIGH
                   ? _dev_bme680_calc_gas_resistance_high(adc_gas_res_high,
                                                          gas_range_h)
                   : _dev_bme680_calc_gas_resistance_low(adc_gas_res_low,
                                                         gas_range_l, bme680);
  }

  data->temperature_c = (float)temp_x100 / 100.0f;
  data->pressure_pa = (float)pressure_pa;
  data->humidity_pct = (float)humidity_x1000 / 1000.0f;
  data->gas_resistance_ohms = (float)gas_ohms;

  return true;
}
