#pragma once
#include <stdint.h>

#define BME680_LEN_COEFF1 23u
#define BME680_LEN_COEFF2 14u
#define BME680_LEN_COEFF3 5u
#define BME680_LEN_COEFF_ALL 42u
#define BME680_LEN_FIELD 17u

#define BME680_FIELD_NEW_DATA_MSK 0x80u
#define BME680_FIELD_GAS_RANGE_MSK 0x0Fu
#define BME680_FIELD_GASM_VALID_MSK 0x20u
#define BME680_FIELD_HEAT_STAB_MSK 0x10u

#define BME680_BIT_H1_DATA_MSK 0x0Fu

#define BME680_RHRANGE_MSK 0x30u
#define BME680_RSERROR_MSK 0xF0u

#define BME680_HCTRL_MSK 0x08u
#define BME680_HCTRL_POS 3u

#define BME680_NBCONV_MSK 0x0Fu

#define BME680_RUN_GAS_MSK 0x30u
#define BME680_RUN_GAS_POS 4u

#define BME680_OSH_MSK 0x07u

#define BME680_OST_MSK 0xE0u
#define BME680_OST_POS 5u

#define BME680_OSP_MSK 0x1Cu
#define BME680_OSP_POS 2u

#define BME680_FILTER_MSK 0x1Cu
#define BME680_FILTER_POS 2u

#define BME680_MODE_MSK 0x03u
#define BME680_FORCED_MODE 0x01u

#define BME680_ENABLE_HEATER 0x00u
#define BME680_ENABLE_GAS_MEAS_LOW 0x01u
#define BME680_ENABLE_GAS_MEAS_HIGH 0x02u

#define BME680_OS_MAX 5u
#define BME680_FILTER_MAX 7u

#define BME680_MEAS_RETRY_COUNT 60u
#define BME680_MEAS_POLL_MS 5u

#define BME680_IO_RETRY_COUNT 3u
#define BME680_IO_RETRY_DELAY_MS 2u

#define BME680_CONCAT_BYTES(msb, lsb) (((uint16_t)(msb) << 8) | (uint16_t)(lsb))

#define BME680_SET_BITS(reg_data, msk, pos, data)                              \
  (((reg_data) & ~(msk)) | ((((data) << (pos)) & (msk))))

#define BME680_SET_BITS_POS_0(reg_data, msk, data)                             \
  (((reg_data) & ~(msk)) | ((data) & (msk)))
